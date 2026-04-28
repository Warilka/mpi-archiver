#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "file_io.h"
#include "compress.h"

#define HEADER_SIZE (sizeof(int) * 2)

int main(int argc, char* argv[]) {
    int rank, size;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    if (argc < 3) {
        if (rank == 0) {
            printf("Usage: %s <compress|decompress> <input_file> [output_file]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }
    
    char* mode = argv[1];
    char* input_file = argv[2];
    char* output_file = argv[3] ? argv[3] : "output.bin";
    
    if (strcmp(mode, "compress") == 0) {
        unsigned char* data = NULL;
        long file_size = 0;
        
        if (rank == 0) {
            data = read_file(input_file, &file_size);
            if (!data) MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        MPI_Bcast(&file_size, 1, MPI_LONG, 0, MPI_COMM_WORLD);
        
        long block_size = file_size / size;
        long remainder = file_size % size;
        long my_block_size = block_size + (rank < remainder ? 1 : 0);
        
        unsigned char* block = (unsigned char*)malloc(my_block_size);
        unsigned char* compressed = (unsigned char*)malloc(compressBound(my_block_size));
        
        int* send_counts = NULL;
        int* displacements = NULL;
        
        if (rank == 0) {
            send_counts = (int*)malloc(size * sizeof(int));
            displacements = (int*)malloc(size * sizeof(int));
            long offset = 0;
            for (int i = 0; i < size; i++) {
                send_counts[i] = block_size + (i < remainder ? 1 : 0);
                displacements[i] = offset;
                offset += send_counts[i];
            }
        }
        
        MPI_Scatterv(data, send_counts, displacements, MPI_UNSIGNED_CHAR,
                     block, my_block_size, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
        
        int compressed_size = compress_block(block, my_block_size,
                                             compressed, compressBound(my_block_size));
        
        printf("Process %d: %ld -> %d bytes\n", rank, my_block_size, compressed_size);
        
        int* all_sizes = (int*)malloc(size * sizeof(int));
        MPI_Gather(&compressed_size, 1, MPI_INT, all_sizes, 1, MPI_INT, 0, MPI_COMM_WORLD);
        
        int* recv_counts = NULL;
        int* recv_disps = NULL;
        unsigned char* all_compressed = NULL;
        
        if (rank == 0) {
            recv_counts = (int*)malloc(size * sizeof(int));
            recv_disps = (int*)malloc(size * sizeof(int));
            int total = 0;
            for (int i = 0; i < size; i++) {
                recv_counts[i] = all_sizes[i];
                recv_disps[i] = total;
                total += all_sizes[i];
            }
            all_compressed = (unsigned char*)malloc(HEADER_SIZE + total);
            memcpy(all_compressed, &file_size, sizeof(int));
            memcpy(all_compressed + sizeof(int), &size, sizeof(int));
        }
        
        MPI_Gatherv(compressed, compressed_size, MPI_UNSIGNED_CHAR,
                    all_compressed + HEADER_SIZE, recv_counts, recv_disps,
                    MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
        
        if (rank == 0) {
            int total_size = HEADER_SIZE;
            for (int i = 0; i < size; i++) total_size += all_sizes[i];
            
            write_file(output_file, all_compressed, total_size);
            printf("Done: %ld -> %d bytes (%.1f%%)\n",
                   file_size, total_size, (1.0 - (float)total_size / file_size) * 100);
            
            free(data);
            free(all_compressed);
            free(recv_counts);
            free(recv_disps);
        }
        
        free(block);
        free(compressed);
        free(all_sizes);
        if (rank == 0) {
            free(send_counts);
            free(displacements);
        }
        
    } else {
        if (rank == 0) printf("Mode '%s' not implemented yet.\n", mode);
    }
    
    MPI_Finalize();
    return 0;
}