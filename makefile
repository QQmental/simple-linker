export PATH+=$PATH:/opt/riscv/bin

link_test_args = -plugin \
	/opt/riscv/libexec/gcc/riscv64-unknown-elf/13.2.0/liblto_plugin.so \
	-plugin-opt=/opt/riscv/libexec/gcc/riscv64-unknown-elf/13.2.0/lto-wrapper \
	-plugin-opt=-fresolution=/tmp/ccBjxiFo.res \
	-plugin-opt=-pass-through=-lgcc \
	-plugin-opt=-pass-through=-lc \
	-plugin-opt=-pass-through=-lgloss \
	-plugin-opt=-pass-through=-lgcc \
	--sysroot=/opt/riscv/riscv64-unknown-elf \
	-melf64lriscv \
	-o \
	test3.elf \
	/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0/../../../../riscv64-unknown-elf/lib/rv64imac/lp64/crt0.o \
	/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0/rv64imac/lp64/crtbegin.o \
	-L/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0/rv64imac/lp64 \
	-L/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0/../../../../riscv64-unknown-elf/lib/rv64imac/lp64 \
	-L/opt/riscv/riscv64-unknown-elf/lib/rv64imac/lp64 \
	-L. \
	-L/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0 \
	-L/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0/../../../../riscv64-unknown-elf/lib \
	-L/opt/riscv/riscv64-unknown-elf/lib \
	test3.o \
	-lgcc \
	--start-group \
	-lc \
	-lgloss \
	--end-group \
	-lgcc \
	/opt/riscv/lib/gcc/riscv64-unknown-elf/13.2.0/rv64imac/lp64/crtend.o

CC = g++

CPP_FLAG = -std=c++17 -pedantic -Wall -MMD -g

SRCS = Relocatable_file.cpp

OBJS = $(SRCS:%.cpp=%.o)

DEPS = $(OBJS:%.o=%.d)

BINS = ld gdb_ld ld2

all: $(BINS)
	riscv64-unknown-elf-gcc test3.c -g -O0 -march=rv64imafc -mabi=lp64 -c -o test3.o
	riscv64-unknown-elf-gcc test3.o -B. -static -g -O0 -march=rv64imafc -mabi=lp64 -o test3.elf	

example.o:example.cpp
	$(CC) $< $(CPP_FLAG) -c -o $@

main.o:main.cpp
	$(CC) $< $(CPP_FLAG) -c -o $@

Relocatable_file.o: Relocatable_file.cpp
	$(CC) $< $(CPP_FLAG) -c -o $@

ld: example.o $(OBJS)
	$(CC) $^ -fsanitize=undefined,address $(CPP_FLAG) -o $@

ld2: main.o $(OBJS)
	$(CC) $^ -fsanitize=undefined,address $(CPP_FLAG) -o $@

gdb_ld: main.o $(OBJS)
	$(CC) $^ -fsanitize=undefined $(CPP_FLAG) -o $@

linking: ld2
	./ld2 $(link_test_args)

run_gdb: gdb_ld
	gdb --args ./gdb_ld $(link_test_args)
	
clean:
	$(shell rm -rf ./test3.elf)
	$(shell rm -rf $(wildcard ./*.d))
	$(shell rm -rf $(wildcard ./*.o))
	$(shell rm -rf $(BINS))

-include $(DEPS)