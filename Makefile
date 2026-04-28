CC = mpicc
CFLAGS = -Wall -O2 -I/storage/user/libs/zlib/include
LDFLAGS = -L/storage/user/libs/zlib/lib -lz -Wl,-rpath,/storage/user/libs/zlib/lib
TARGET = archiver
OBJS = main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f $(TARGET) *.o *.bin

run1:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 1 --host master ./$(TARGET) test.txt out.bin

run2:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 2 --host master,n01 ./$(TARGET) test.txt out.bin

run4:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 4 --host master,n01,n02,n03 ./$(TARGET) test.txt out.bin

.PHONY: all clean run1 run2 run4