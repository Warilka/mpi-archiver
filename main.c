#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>

#define BLOCK_SIZE (128 * 1024 * 1024)  // 128 МБ на процесс

int main(int argc, char* argv[]) {
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc < 2) {
        if (rank == 0) {
            printf("Usage: %s <output_file>\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    char* output_file = argv[1];

    double start_time = MPI_Wtime();

    srand(time(NULL) + rank);

    unsigned char* data = (unsigned char*)malloc(BLOCK_SIZE);
    unsigned char* compressed = (unsigned char*)malloc(compressBound(BLOCK_SIZE));

    if (!data || !compressed) {
        printf("Process %d: Memory allocation error\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    double gen_start = MPI_Wtime();
    for (long i = 0; i < BLOCK_SIZE; i++) {
        data[i] = rand() % 256;
    }
    double gen_end = MPI_Wtime();

    double comp_start = MPI_Wtime();
    uLongf compressed_size = compressBound(BLOCK_SIZE);
    int result = compress(compressed, &compressed_size, data, BLOCK_SIZE);
    double comp_end = MPI_Wtime();

    if (result != Z_OK) {
        printf("Process %d: Compression error\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    double gen_time = gen_end - gen_start;
    double comp_time = comp_end - comp_start;

    printf("Process %d: generated %d MB in %.3fs, compressed to %lu bytes in %.3fs\n",
        rank, BLOCK_SIZE / (1024 * 1024), gen_time, compressed_size, comp_time);

    unsigned long* all_sizes = (unsigned long*)malloc(size * sizeof(unsigned long));

    MPI_Gather(&compressed_size, 1, MPI_UNSIGNED_LONG,
        all_sizes, 1, MPI_UNSIGNED_LONG,
        0, MPI_COMM_WORLD);

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

        all_compressed = (unsigned char*)malloc(total + sizeof(int) * 2);
        int header_block = BLOCK_SIZE;
        memcpy(all_compressed, &header_block, sizeof(int));
        memcpy(all_compressed + sizeof(int), &size, sizeof(int));
    }

    MPI_Gatherv(compressed, compressed_size, MPI_UNSIGNED_CHAR,
        all_compressed + sizeof(int) * 2, recv_counts, recv_disps,
        MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    double end_time = MPI_Wtime();

    if (rank == 0) {
        unsigned long total_original = (unsigned long)BLOCK_SIZE * size;
        unsigned long total_compressed = sizeof(int) * 2;
        for (int i = 0; i < size; i++) {
            total_compressed += all_sizes[i];
        }

        printf("Total original:  %lu bytes (%lu MB)\n", total_original, total_original / (1024 * 1024));
        printf("Total compressed: %lu bytes\n", total_compressed);
        printf("Total time:       %.4f seconds\n", end_time - start_time);
        printf("Throughput:       %.2f MB/s\n", total_original / (1024.0 * 1024.0) / (end_time - start_time));

        FILE* f = fopen(output_file, "wb");
        if (f) {
            fwrite(all_compressed, 1, total_compressed, f);
            fclose(f);
        }

        free(all_compressed);
        free(recv_counts);
        free(recv_disps);
    }

    free(data);
    free(compressed);
    free(all_sizes);

    MPI_Finalize();
    return 0;
}