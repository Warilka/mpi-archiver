#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BUF_SIZE (8 * 1024 * 1024)

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

    // === Узнаём размер файла ===
    long total_size = 0;
    if (rank == 0) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) { printf("Cannot open file\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        fseek(f, 0, SEEK_END);
        total_size = ftell(f);
        fclose(f);
    }
    MPI_Bcast(&total_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    // === Вычисляем свой блок ===
    long block_size = total_size / size;
    long remainder = total_size % size;
    long my_offset = rank * block_size + (rank < remainder ? rank : remainder);
    long my_size = block_size + (rank < remainder ? 1 : 0);

    // === Потоковое сжатие ===
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);

    unsigned char* read_buf = (unsigned char*)malloc(BUF_SIZE);
    unsigned char* comp_buf = (unsigned char*)malloc(compressBound(my_size));

    strm.avail_out = compressBound(my_size);
    strm.next_out = comp_buf;

    FILE* f = fopen(argv[1], "rb");
    fseek(f, my_offset, SEEK_SET);

    long remaining = my_size;
    int flush = Z_NO_FLUSH;

    while (remaining > 0) {
        long to_read = (remaining < BUF_SIZE) ? remaining : BUF_SIZE;
        size_t bytes_read = fread(read_buf, 1, to_read, f);
        if (bytes_read == 0) break;

        remaining -= bytes_read;
        if (remaining == 0) flush = Z_FINISH;

        strm.avail_in = bytes_read;
        strm.next_in = read_buf;

        int ret;
        do {
            ret = deflate(&strm, flush);
        } while (ret == Z_OK && strm.avail_out > 100);
    }

    fclose(f);
    deflateEnd(&strm);

    long comp_size = strm.total_out;

    // === Асинхронный сбор размеров ===
    unsigned long* all_sizes = (unsigned long*)malloc(size * sizeof(unsigned long));
    unsigned long my_comp = (unsigned long)comp_size;

    MPI_Request req_size;
    MPI_Iallgather(&my_comp, 1, MPI_UNSIGNED_LONG,
        all_sizes, 1, MPI_UNSIGNED_LONG,
        MPI_COMM_WORLD, &req_size);

    // Пока MPI собирает размеры — можем освобождать буферы
    free(read_buf);

    MPI_Wait(&req_size, MPI_STATUS_IGNORE);

    // === Асинхронный сбор сжатых данных ===
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

    MPI_Request req_data;
    MPI_Igatherv(comp_buf, comp_size, MPI_UNSIGNED_CHAR,
        all_compressed + sizeof(long) + sizeof(int),
        recv_counts, recv_disps, MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD, &req_data);

    // Пока MPI собирает данные — освобождаем comp_buf
    free(comp_buf);

    MPI_Wait(&req_data, MPI_STATUS_IGNORE);

    double end_time = MPI_Wtime();

    // === Запись ===
    if (rank == 0) {
        unsigned long total_comp = sizeof(long) + sizeof(int);
        for (int i = 0; i < size; i++) total_comp += all_sizes[i];

        FILE* fout = fopen(argv[2], "wb");
        fwrite(all_compressed, 1, total_comp, fout);
        fclose(fout);

        printf("File: %s\n", argv[1]);
        printf("Original: %ld bytes (%.1f MB)\n", total_size, total_size / (1024.0 * 1024.0));
        printf("Compressed: %lu bytes (%.1f%%)\n", total_comp,
            (1.0 - (double)total_comp / total_size) * 100);
        printf("Processes: %d | Buffer: %d MB\n", size, BUF_SIZE / (1024 * 1024));
        printf("Time: %.4f sec | Speed: %.2f MB/s\n",
            end_time - start_time, total_size / (1024.0 * 1024.0) / (end_time - start_time));
        printf("Features: streaming + async Iallgather + Igatherv\n");

        free(all_compressed);
        free(recv_counts);
        free(recv_disps);
    }

    free(all_sizes);

    MPI_Finalize();
    return 0;
}