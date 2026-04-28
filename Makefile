CC = mpicc
CFLAGS = -Wall -O2 -I/storage/user/libs/zlib/include
LDFLAGS = -L/storage/user/libs/zlib/lib -lz -Wl,-rpath,/storage/user/libs/zlib/lib
TARGET = archiver
OBJS = main.o file_io.o compress.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

main.o: main.c file_io.h compress.h
	$(CC) $(CFLAGS) -c main.c

file_io.o: file_io.c file_io.h
	$(CC) $(CFLAGS) -c file_io.c

compress.o: compress.c compress.h
	$(CC) $(CFLAGS) -c compress.c

clean:
	rm -f $(TARGET) *.o compressed.bin

run2:
	mpirun -np 2 --host master,n01 ./$(TARGET) compress test.txt compressed.bin

run4:
	mpirun -np 4 --host master,n01,n02,n03 ./$(TARGET) compress test.txt compressed.bin

.PHONY: all clean run2 run4