#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>

unsigned char* read_file(const char* filename, long* file_size) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("Cannot open file: %s\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char* data = (unsigned char*)malloc(*file_size);
    if (!data) {
        printf("Memory allocation error\n");
        fclose(f);
        return NULL;
    }
    fread(data, 1, *file_size, f);
    fclose(f);
    return data;
}

int write_file(const char* filename, const unsigned char* data, long size) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        printf("Cannot create file: %s\n", filename);
        return -1;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    return 0;
}