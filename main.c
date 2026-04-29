#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BUF_SIZE (8 * 1024 * 1024)  // 8 МБ — размер буфера чтения

int main(int argc, char* argv[]) {
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc < 3) {
        if (rank == 0) printf("Usage: %s <input> <output>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    double start_time = MPI_Wtime();

    // === Шаг 1: rank 0 узнаёт размер файла ===
    long total_size = 0;
    if (rank == 0) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) { printf("Cannot open file\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        fseek(f, 0, SEEK_END);
        total_size = ftell(f);
        fclose(f);
    }

    MPI_Bcast(&total_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    // === Шаг 2: вычисляем свой диапазон ===
    long block_size = total_size / size;
    long remainder = total_size % size;
    long my_offset = rank * block_size + (rank < remainder ? rank : remainder);
    long my_size = block_size + (rank < remainder ? 1 : 0);
    long my_end = my_offset + my_size;

    printf("Process %d: range [%ld - %ld), total %ld bytes\n",
        rank, my_offset, my_end, my_size);

    // === Шаг 3: буферизованное чтение и сжатие ===

    // Инициализация zlib-потока
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);

    // Выделяем память под буфер чтения и сжатый результат
    unsigned char* read_buf = (unsigned char*)malloc(BUF_SIZE);
    unsigned char* comp_buf = (unsigned char*)malloc(compressBound(my_size));

    strm.avail_out = compressBound(my_size);
    strm.next_out = comp_buf;

    // Открываем файл
    FILE* f = fopen(argv[1], "rb");
    fseek(f, my_offset, SEEK_SET);

    long remaining = my_size;
    long total_read = 0;
    long total_compressed = 0;
    int flush = Z_NO_FLUSH;

    while (remaining > 0) {
        long to_read = (remaining < BUF_SIZE) ? remaining : BUF_SIZE;
        size_t bytes_read = fread(read_buf, 1, to_read, f);

        if (bytes_read == 0) break;

        remaining -= bytes_read;
        total_read += bytes_read;

        if (remaining == 0) flush = Z_FINISH;

        strm.avail_in = bytes_read;
        strm.next_in = read_buf;

        int ret;
        do {
            ret = deflate(&strm, flush);
        } while (ret == Z_OK && strm.avail_out > 0);

        if (ret == Z_STREAM_ERROR) {
            printf("Process %d: compression error\n", rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    fclose(f);
    deflateEnd(&strm);

    // Размер сжатых данных
    long comp_size = strm.total_out;

    printf("Process %d: read %ld bytes in chunks, compressed to %ld bytes\n",
        rank, total_read, comp_size);

    // === Шаг 4: собираем сжатые данные ===
    unsigned long* all_sizes = (unsigned long*)malloc(size * sizeof(unsigned long));
    unsigned long my_comp = (unsigned long)comp_size;
    MPI_Gather(&my_comp, 1, MPI_UNSIGNED_LONG, all_sizes, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

    int* recv_counts = NULL;
    int* recv_disps = NULL;
    unsigned char* all_compressed = NULL;

    if (rank == 0) {
        recv_counts = (int*)malloc(size * sizeof(int));
        recv_disps = (int*)malloc(size * sizeof(int));
        unsigned long total = 0;
        for (int i = 0; i < size; i++) {
            recv_counts[i] = (int)all_sizes[i];
            recv_disps[i] = total;
            total += all_sizes[i];
        }
        all_compressed = (unsigned char*)malloc(total + sizeof(long) + sizeof(int));
        memcpy(all_compressed, &total_size, sizeof(long));
        memcpy(all_compressed + sizeof(long), &size, sizeof(int));
    }

    MPI_Gatherv(comp_buf, comp_size, MPI_UNSIGNED_CHAR,
        all_compressed + sizeof(long) + sizeof(int),
        recv_counts, recv_disps, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    double end_time = MPI_Wtime();

    // === Шаг 5: запись ===
    if (rank == 0) {
        unsigned long total_comp = sizeof(long) + sizeof(int);
        for (int i = 0; i < size; i++) total_comp += all_sizes[i];

        FILE* fout = fopen(argv[2], "wb");
        fwrite(all_compressed, 1, total_comp, fout);
        fclose(fout);

        printf("\n========================================\n");
        printf("File: %s\n", argv[1]);
        printf("Original: %ld bytes (%.1f MB)\n", total_size, total_size / (1024.0 * 1024.0));
        printf("Compressed: %lu bytes\n", total_comp);
        printf("Ratio: %.1f%%\n", (1.0 - (double)total_comp / total_size) * 100);
        printf("Processes: %d\n", size);
        printf("Time: %.4f seconds\n", end_time - start_time);
        printf("Throughput: %.2f MB/s\n", total_size / (1024.0 * 1024.0) / (end_time - start_time));
        printf("Buffer size: %d MB\n", BUF_SIZE / (1024 * 1024));
        printf("========================================\n");

        free(all_compressed);
        free(recv_counts);
        free(recv_disps);
    }

    free(read_buf);
    free(comp_buf);
    free(all_sizes);

    MPI_Finalize();
    return 0;
}