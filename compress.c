#include "compress.h"
#include <zlib.h>
#include <stdio.h>

int compress_block(const unsigned char* original, int original_size,
                   unsigned char* compressed, int max_compressed_size) {
    uLongf dest_len = max_compressed_size;
    int result = compress(compressed, &dest_len, original, original_size);
    if (result != Z_OK) {
        printf("Compression error: %d\n", result);
        return -1;
    }
    return (int)dest_len;
}

int decompress_block(const unsigned char* compressed, int compressed_size,
                     unsigned char* output, int original_size) {
    uLongf dest_len = original_size;
    int result = uncompress(output, &dest_len, compressed, compressed_size);
    if (result != Z_OK) {
        printf("Decompression error: %d\n", result);
        return -1;
    }
    return (int)dest_len;
}