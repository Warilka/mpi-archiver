#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BUF_SIZE (32 * 1024 * 1024)
#define COMPRESSION_LEVEL Z_BEST_SPEED

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

    // === Размер файла ===
    long total_size = 0;
    if (rank == 0) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) { printf("Cannot open file\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        fseek(f, 0, SEEK_END);
        total_size = ftell(f);
        fclose(f);
    }
    MPI_Bcast(&total_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    // === Распределение блоков ===
    long block_size = total_size / size;
    long remainder = total_size % size;
    long my_offset = rank * block_size + (rank < remainder ? rank : remainder);
    long my_size = block_size + (rank < remainder ? 1 : 0);

    // === Чтение и сжатие ===
    unsigned char* compressed = NULL;
    int comp_size = 0;

    if (my_size > 0) {
        // Читаем блок
        unsigned char* data = (unsigned char*)malloc(my_size);
        FILE* f = fopen(argv[1], "rb");
        fseek(f, my_offset, SEEK_SET);
        fread(data, 1, my_size, f);
        fclose(f);

        // Сжимаем
        comp_size = compressBound(my_size) + 1024;
        compressed = (unsigned char*)malloc(comp_size);
        uLongf dest_len = comp_size;
        compress2(compressed, &dest_len, data, my_size, COMPRESSION_LEVEL);
        comp_size = dest_len;

        free(data);
    }
    else {
        compressed = (unsigned char*)malloc(1);
        comp_size = 0;
    }

    // === Сбор размеров ===
    int* all_sizes = (rank == 0) ? (int*)malloc(size * sizeof(int)) : NULL;
    MPI_Gather(&comp_size, 1, MPI_INT, all_sizes, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // === Вычисление смещений ===
    int* disps = NULL;
    if (rank == 0) {
        disps = (int*)malloc(size * sizeof(int));
        int offset = sizeof(long) + sizeof(int);  // заголовок
        for (int i = 0; i < size; i++) {
            disps[i] = offset;
            offset += all_sizes[i];
        }
    }
    MPI_Bcast(disps, size, MPI_INT, 0, MPI_COMM_WORLD);

    // === MPI-I/O запись ===
    MPI_File fh;
    MPI_File_open(MPI_COMM_WORLD, argv[2], MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);

    if (comp_size > 0) {
        MPI_File_write_at_all(fh, disps[rank], compressed, comp_size, MPI_UNSIGNED_CHAR, MPI_STATUS_IGNORE);
    }

    if (rank == 0) {
        MPI_File_write_at(fh, 0, &total_size, sizeof(long), MPI_UNSIGNED_CHAR, MPI_STATUS_IGNORE);
        MPI_File_write_at(fh, sizeof(long), &size, sizeof(int), MPI_UNSIGNED_CHAR, MPI_STATUS_IGNORE);
    }

    MPI_File_close(&fh);

    // === Вывод ===
    if (rank == 0) {
        double elapsed = MPI_Wtime() - start_time;
        double throughput = total_size / (1024.0 * 1024.0) / elapsed;
        printf("Time: %.2f sec, Throughput: %.2f MB/s\n", elapsed, throughput);
        free(all_sizes);
        free(disps);
    }

    free(compressed);
    MPI_Finalize();
    return 0;
}