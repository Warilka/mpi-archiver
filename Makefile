CC = mpicc
CFLAGS = -O3 -I/storage/user/libs/zlib/include
LDFLAGS = -L/storage/user/libs/zlib/lib -lz -Wl,-rpath,/storage/user/libs/zlib/lib
TARGET = archiver

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET) *.bin

run1:
	mpirun -np 1 --host master ./$(TARGET) test.txt out.bin

run2:
	mpirun -np 2 --host master,n01 ./$(TARGET) test.txt out.bin

run3:
	mpirun -np 3 --host master,n01,n02 ./$(TARGET) test.txt out.bin

run4:
	mpirun -np 4 --host master,n01,n02,n03 ./$(TARGET) test.txt out.bin

.PHONY: all clean run1 run2 run3 run4