#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

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

    // Рассылаем только размер файла
    MPI_Bcast(&total_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    // === Шаг 2: вычисляем свой блок ===
    long block_size = total_size / size;
    long remainder = total_size % size;
    long my_offset = rank * block_size + (rank < remainder ? rank : remainder);
    long my_size = block_size + (rank < remainder ? 1 : 0);

    // === Шаг 3: читаем свой блок напрямую с общего диска ===
    unsigned char* my_data = (unsigned char*)malloc(my_size);
    unsigned char* compressed = (unsigned char*)malloc(compressBound(my_size));

    FILE* f = fopen(argv[1], "rb");
    fseek(f, my_offset, SEEK_SET);
    fread(my_data, 1, my_size, f);
    fclose(f);

    //printf("Process %d: offset=%ld, size=%ld, read done\n", rank, my_offset, my_size);

    // === Шаг 4: сжимаем ===
    uLongf comp_size = compressBound(my_size);
    compress(compressed, &comp_size, my_data, my_size);

    //printf("Process %d: compressed %ld -> %lu bytes\n", rank, my_size, comp_size);

    // === Шаг 5: собираем только сжатые данные ===
    unsigned long* all_sizes = (unsigned long*)malloc(size * sizeof(unsigned long));
    MPI_Gather(&comp_size, 1, MPI_UNSIGNED_LONG, all_sizes, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

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

    MPI_Gatherv(compressed, comp_size, MPI_UNSIGNED_CHAR,
        all_compressed + sizeof(long) + sizeof(int),
        recv_counts, recv_disps, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    double end_time = MPI_Wtime();

    // === Шаг 6: запись результата ===
    if (rank == 0) {
        unsigned long total_comp = sizeof(long) + sizeof(int);
        for (int i = 0; i < size; i++) total_comp += all_sizes[i];

        FILE* fout = fopen(argv[2], "wb");
        fwrite(all_compressed, 1, total_comp, fout);
        fclose(fout);

        printf("File: %s\n", argv[1]);
        printf("Original: %ld bytes (%.1f MB)\n", total_size, total_size / (1024.0 * 1024.0));
        printf("Compressed: %lu bytes\n", total_comp);
        printf("Ratio: %.1f%%\n", (1.0 - (double)total_comp / total_size) * 100);
        printf("Processes: %d\n", size);
        printf("Time: %.4f seconds\n", end_time - start_time);
        printf("Throughput: %.2f MB/s\n", total_size / (1024.0 * 1024.0) / (end_time - start_time));

        free(all_compressed);
        free(recv_counts);
        free(recv_disps);
    }

    free(my_data);
    free(compressed);
    free(all_sizes);

    MPI_Finalize();
    return 0;
}