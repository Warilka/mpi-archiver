#ifndef FILE_IO_H
#define FILE_IO_H

#include <stddef.h>

unsigned char* read_file(const char* filename, long* file_size);
int write_file(const char* filename, const unsigned char* data, long size);

#endif