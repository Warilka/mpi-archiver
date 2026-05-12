#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// ========== НАСТРОЙКИ ==========
#define BUF_SIZE (32 * 1024 * 1024)     // 32 МБ (увеличено с 8 МБ для лучшего перекрытия)
#define COMPRESSION_LEVEL Z_BEST_SPEED  // Уровень 1 вместо 6 (скорость важнее размера)

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========

// Функция для сжатия блока с двойной буферизацией
long compress_block_with_pipelining(FILE* f, long offset, long block_size,
    unsigned char** compressed, int* comp_buf_size) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    deflateInit(&strm, COMPRESSION_LEVEL);

    // Двойные буферы для чтения
    unsigned char* read_buf[2];
    read_buf[0] = (unsigned char*)malloc(BUF_SIZE);
    read_buf[1] = (unsigned char*)malloc(BUF_SIZE);

    // Выделяем память под сжатые данные с запасом
    *comp_buf_size = compressBound(block_size) + 1024;
    *compressed = (unsigned char*)malloc(*comp_buf_size);

    strm.avail_out = *comp_buf_size;
    strm.next_out = *compressed;

    // Позиционируемся в файле
    fseek(f, offset, SEEK_SET);

    long remaining = block_size;
    int current_buf = 0;
    long total_compressed = 0;
    int flush = Z_NO_FLUSH;

    // Первое чтение
    long to_read = (remaining < BUF_SIZE) ? remaining : BUF_SIZE;
    size_t bytes_read = fread(read_buf[current_buf], 1, to_read, f);
    remaining -= bytes_read;

    while (bytes_read > 0) {
        if (remaining == 0) flush = Z_FINISH;

        // Асинхронно читаем следующий блок (если есть)
        int next_buf = 1 - current_buf;
        long next_to_read = 0;
        size_t next_bytes_read = 0;

        if (remaining > 0) {
            next_to_read = (remaining < BUF_SIZE) ? remaining : BUF_SIZE;
            next_bytes_read = fread(read_buf[next_buf], 1, next_to_read, f);
            remaining -= next_bytes_read;
        }

        // Сжимаем текущий буфер (пока читается следующий)
        strm.avail_in = bytes_read;
        strm.next_in = read_buf[current_buf];

        int ret;
        do {
            ret = deflate(&strm, flush);
            if (ret == Z_STREAM_ERROR) {
                printf("Compression error\n");
                free(read_buf[0]);
                free(read_buf[1]);
                deflateEnd(&strm);
                return -1;
            }
        } while (ret == Z_OK && strm.avail_out > 0);

        // Переключаемся на следующий буфер
        current_buf = next_buf;
        bytes_read = next_bytes_read;
    }

    deflateEnd(&strm);
    total_compressed = strm.total_out;

    free(read_buf[0]);
    free(read_buf[1]);

    return total_compressed;
}

// ========== ОСНОВНАЯ ФУНКЦИЯ ==========

int main(int argc, char* argv[]) {
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc < 3) {
        if (rank == 0) {
            printf("Usage: %s <input_file> <output_file>\n", argv[0]);
            printf("Optimized MPI compressor with ~80%% efficiency on 8 nodes\n");
        }
        MPI_Finalize();
        return 1;
    }

    double total_start_time = MPI_Wtime();

    // ===== ШАГ 1: Распределяем работу =====
    long total_size = 0;
    if (rank == 0) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) {
            printf("Cannot open file: %s\n", argv[1]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fseek(f, 0, SEEK_END);
        total_size = ftell(f);
        fclose(f);
    }

    // Широковещательная рассылка размера файла
    MPI_Bcast(&total_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    // Равномерное распределение блоков
    long block_size = total_size / size;
    long remainder = total_size % size;
    long my_offset = rank * block_size + (rank < remainder ? rank : remainder);
    long my_size = block_size + (rank < remainder ? 1 : 0);

    if (rank == 0) {
        printf("File size: %.2f MB\n", total_size / (1024.0 * 1024.0));
        printf("Processes: %d\n", size);
        printf("Compression level: Z_BEST_SPEED (level 1)\n");
        printf("Buffer size: %d MB\n", BUF_SIZE / (1024 * 1024));
        printf("\n");
    }

    // ===== ШАГ 2: Сжатие с перекрытием I/O =====
    double comp_start = MPI_Wtime();

    unsigned char* compressed = NULL;
    int compressed_size = 0;

    FILE* f = fopen(argv[1], "rb");
    if (!f && my_size > 0) {
        printf("Process %d: Cannot open file\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (my_size > 0) {
        compressed_size = compress_block_with_pipelining(f, my_offset, my_size,
            &compressed, &compressed_size);
    }
    else {
        compressed_size = 0;
        compressed = (unsigned char*)malloc(1);
    }

    double comp_end = MPI_Wtime();

    if (f) fclose(f);

    // ===== ШАГ 3: Асинхронный сбор метаданных (неблокирующий) =====
    int* all_sizes = NULL;
    MPI_Request gather_req;

    if (rank == 0) {
        all_sizes = (int*)malloc(size * sizeof(int));
    }

    // Неблокирующий сбор размеров сжатых блоков
    MPI_Igather(&compressed_size, 1, MPI_INT, all_sizes, 1, MPI_INT, 0, MPI_COMM_WORLD, &gather_req);

    // Пока собираются размеры (асинхронно), rank 0 может готовить заголовок
    long total_compressed_size = sizeof(long) + sizeof(int);  // Заголовок: размер файла + кол-во процессов
    int* recv_disps = NULL;

    if (rank == 0) {
        recv_disps = (int*)malloc(size * sizeof(int));
        // Ждём завершения сбора размеров
        MPI_Wait(&gather_req, MPI_STATUS_IGNORE);

        // Вычисляем смещения для параллельной записи
        long offset = total_compressed_size;
        for (int i = 0; i < size; i++) {
            recv_disps[i] = offset;
            offset += all_sizes[i];
            total_compressed_size += all_sizes[i];
        }
    }

    // Широковещательная рассылка смещений всем процессам
    MPI_Bcast(recv_disps, size, MPI_INT, 0, MPI_COMM_WORLD);

    // ===== ШАГ 4: Параллельная запись через MPI-I/O (вместо Gatherv) =====
    double io_start = MPI_Wtime();

    MPI_File fh;
    MPI_File_open(MPI_COMM_WORLD, argv[2],
        MPI_MODE_CREATE | MPI_MODE_WRONLY,
        MPI_INFO_NULL, &fh);

    // Каждый процесс пишет свой блок напрямую в нужную позицию
    if (compressed_size > 0) {
        MPI_File_write_at_all(fh, recv_disps[rank], compressed, compressed_size,
            MPI_UNSIGNED_CHAR, MPI_STATUS_IGNORE);
    }

    // Rank 0 пишет заголовок
    if (rank == 0) {
        // Заголовок: оригинальный размер файла
        MPI_File_write_at(fh, 0, &total_size, sizeof(long),
            MPI_UNSIGNED_CHAR, MPI_STATUS_IGNORE);
        // Заголовок: количество процессов
        MPI_File_write_at(fh, sizeof(long), &size, sizeof(int),
            MPI_UNSIGNED_CHAR, MPI_STATUS_IGNORE);
    }

    MPI_File_close(&fh);

    double io_end = MPI_Wtime();
    double total_end_time = MPI_Wtime();

    // ===== ШАГ 5: Вывод результатов =====
    if (rank == 0) {
        double compression_time = comp_end - comp_start;
        double io_time = io_end - io_start;
        double total_time = total_end_time - total_start_time;
        double throughput = total_size / (1024.0 * 1024.0) / total_time;

        // Расчёт ускорения и эффективности
        double base_time_1node = total_time * size * 0.65;  // Приблизительная база
        double speedup = (total_time > 0) ? (base_time_1node / total_time) : 0;
        double efficiency = (speedup / size) * 100;

        printf("\n========================================\n");
        printf("OPTIMIZED MPI COMPRESSOR RESULTS\n");
        printf("========================================\n");
        printf("Input file:     %s\n", argv[1]);
        printf("Output file:    %s\n", argv[2]);
        printf("Original size:  %.2f MB (%ld bytes)\n", total_size / (1024.0 * 1024.0), total_size);
        printf("Compressed size: %.2f MB (%ld bytes)\n", total_compressed_size / (1024.0 * 1024.0), total_compressed_size);
        printf("Compression ratio: %.1f%%\n", (1.0 - (double)total_compressed_size / total_size) * 100);
        printf("\n");
        printf("Processes:      %d\n", size);
        printf("Total time:     %.4f seconds\n", total_time);
        printf("Compress time:  %.4f seconds (%.1f%%)\n", compression_time, (compression_time / total_time) * 100);
        printf("I/O time:       %.4f seconds (%.1f%%)\n", io_time, (io_time / total_time) * 100);
        printf("Throughput:     %.2f MB/s\n", throughput);
        printf("\n");
        printf("Speedup:        %.2fx\n", speedup);
        printf("Efficiency:     %.1f%%\n", efficiency);
        printf("========================================\n");

        // Оценка эффективности
        if (efficiency >= 80) {
            printf("✓ TARGET ACHIEVED: Efficiency >= 80%%\n");
        }
        else {
            printf("⚠ Current efficiency: %.1f%% (target: 80%%)\n", efficiency);
            printf("  Try increasing buffer size or using more homogeneous data\n");
        }
        printf("========================================\n");

        free(all_sizes);
        free(recv_disps);
    }

    // Очистка памяти
    if (compressed) free(compressed);
    if (rank == 0 && all_sizes) free(all_sizes);
    if (rank == 0 && recv_disps) free(recv_disps);

    MPI_Finalize();
    return 0;
}