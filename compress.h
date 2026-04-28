#ifndef COMPRESS_H
#define COMPRESS_H

#include <stddef.h>

int compress_block(const unsigned char* original, int original_size,
                   unsigned char* compressed, int max_compressed_size);

int decompress_block(const unsigned char* compressed, int compressed_size,
                     unsigned char* output, int original_size);

#endif