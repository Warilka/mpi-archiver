CC = mpicc
CFLAGS = -Wall -O3 -march=native -mtune=native -funroll-loops -I/storage/user/libs/zlib/include
LDFLAGS = -L/storage/user/libs/zlib/lib -lz -Wl,-rpath,/storage/user/libs/zlib/lib
TARGET = archiver_optimized
OBJS = main_optimized.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

main_optimized.o: main_optimized.c
	$(CC) $(CFLAGS) -c main_optimized.c

clean:
	rm -f $(TARGET) *.o *.bin

# ========== ЗАПУСК НА 1, 2, 3, 4 МАШИНАХ ==========

run1:
	@echo "========================================="
	@echo "Running on 1 machine (master)"
	@echo "========================================="
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 1 --host master ./$(TARGET) test.txt out_1p.bin

run2:
	@echo "========================================="
	@echo "Running on 2 machines (master, n01)"
	@echo "========================================="
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 2 --host master,n01 ./$(TARGET) test.txt out_2p.bin

run3:
	@echo "========================================="
	@echo "Running on 3 machines (master, n01, n02)"
	@echo "========================================="
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 3 --host master,n01,n02 ./$(TARGET) test.txt out_3p.bin

run4:
	@echo "========================================="
	@echo "Running on 4 machines (master, n01, n02, n03)"
	@echo "========================================="
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 4 --host master,n01,n02,n03 ./$(TARGET) test.txt out_4p.bin

# ========== ЗАПУСК ВСЕХ (ПОСЛЕДОВАТЕЛЬНО) ==========

run-all:
	@echo "========================================="
	@echo "Running benchmarks: 1, 2, 3, 4 machines"
	@echo "========================================="
	$(MAKE) run1
	$(MAKE) run2
	$(MAKE) run3
	$(MAKE) run4
	@echo "========================================="
	@echo "All benchmarks completed!"
	@echo "========================================="

# ========== ЗАПУСК С ПРОИЗВОЛЬНЫМ ФАЙЛОМ ==========

run1-file:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 1 --host master ./$(TARGET) $(FILE) out_1p.bin

run2-file:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 2 --host master,n01 ./$(TARGET) $(FILE) out_2p.bin

run3-file:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 3 --host master,n01,n02 ./$(TARGET) $(FILE) out_3p.bin

run4-file:
	mpirun --mca btl_tcp_if_include 192.168.26.0/24 -np 4 --host master,n01,n02,n03 ./$(TARGET) $(FILE) out_4p.bin

.PHONY: all clean run1 run2 run3 run4 run-all