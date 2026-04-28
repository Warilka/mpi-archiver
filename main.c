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
        if (rank == 0) {
            printf("Usage: %s <input_file> <output_file>\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    char* input_file = argv[1];
    char* output_file = argv[2];

    double start_time = MPI_Wtime();

    // Чтение файла на rank 0
    unsigned char* data = NULL;
    long file_size = 0;

    if (rank == 0) {
        FILE* f = fopen(input_file, "rb");
        if (!f) {
            printf("Cannot open: %s\n", input_file);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fseek(f, 0, SEEK_END);
        file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = (unsigned char*)malloc(file_size);
        fread(data, 1, file_size, f);
        fclose(f);
    }

    // Рассылаем размер
    MPI_Bcast(&file_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);

    // Вычисляем свой блок
    long block_size = file_size / size;
    long remainder = file_size % size;
    long my_size = block_size + (rank < remainder ? 1 : 0);

    unsigned char* my_data = (unsigned char*)malloc(my_size);
    unsigned char* compressed = (unsigned char*)malloc(compressBound(my_size));

    // Рассылаем данные
    int* send_counts = NULL;
    int* send_disps = NULL;
    if (rank == 0) {
        send_counts = (int*)malloc(size * sizeof(int));
        send_disps = (int*)malloc(size * sizeof(int));
        long offset = 0;
        for (int i = 0; i < size; i++) {
            send_counts[i] = block_size + (i < remainder ? 1 : 0);
            send_disps[i] = offset;
            offset += send_counts[i];
        }
    }

    MPI_Scatterv(data, send_counts, send_disps, MPI_UNSIGNED_CHAR,
        my_data, my_size, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    // Сжимаем
    uLongf comp_size = compressBound(my_size);
    compress(compressed, &comp_size, my_data, my_size);

    printf("Process %d: %ld -> %lu bytes\n", rank, my_size, comp_size);

    // Собираем размеры
    unsigned long* all_sizes = (unsigned long*)malloc(size * sizeof(unsigned long));
    MPI_Gather(&comp_size, 1, MPI_UNSIGNED_LONG, all_sizes, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

    // Собираем данные
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
        memcpy(all_compressed, &file_size, sizeof(int));
        memcpy(all_compressed + sizeof(int), &size, sizeof(int));
    }

    MPI_Gatherv(compressed, comp_size, MPI_UNSIGNED_CHAR,
        all_compressed + sizeof(int) * 2, recv_counts, recv_disps,
        MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    double end_time = MPI_Wtime();

    if (rank == 0) {
        unsigned long total_comp = sizeof(int) * 2;
        for (int i = 0; i < size; i++) total_comp += all_sizes[i];

        printf("\n========================================\n");
        printf("File: %s\n", input_file);
        printf("Original: %ld bytes\n", file_size);
        printf("Compressed: %lu bytes (%.1f%%)\n", total_comp,
            (1.0 - (double)total_comp / file_size) * 100);
        printf("Processes: %d\n", size);
        printf("Time: %.4f seconds\n", end_time - start_time);
        printf("========================================\n");

        FILE* f = fopen(output_file, "wb");
        if (f) {
            fwrite(all_compressed, 1, total_comp, f);
            fclose(f);
        }

        free(data);
        free(all_compressed);
        free(recv_counts);
        free(recv_disps);
    }

    free(my_data);
    free(compressed);
    free(all_sizes);
    if (rank == 0) { free(send_counts); free(send_disps); }

    MPI_Finalize();
    return 0;
}