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

CPP_FLAG = -std=c++17 -pedantic -Wall -MMD -O0 -g -Wpedantic -Werror=return-type

INCLUDE = $(addprefix -I,include ./)

Build = build

SRCS = main.cpp \
       Relocatable_file.cpp \
       Linking_context.cpp \
	   Linking_passes.cpp \
       Input_file.cpp \
	   Mergeable_section.cpp \
	   Merged_section.cpp \
	   Output_chunk.cpp

OBJS = $(addprefix $(Build)/,$(SRCS:%.cpp=%.o)) 

DEPS = $(OBJS:%.o=%.d)

BINS = ld gdb_ld

all: $(BINS)
	riscv64-unknown-elf-gcc test3.c -O0 -g -march=rv64imafc -mabi=lp64 -c -o test3.o
	riscv64-unknown-elf-gcc test3.o -O0 -g -B. -march=rv64imafc -mabi=lp64 -o test3.elf	

$(Build)/main.o:src/main.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

$(Build)/Relocatable_file.o: src/Relocatable_file.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

$(Build)/Linking_context.o: src/Linking_context.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

$(Build)/Linking_passes.o: src/Linking_passes.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@	

$(Build)/Input_file.o: src/Input_file.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

$(Build)/Mergeable_section.o: src/Mergeable_section.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

$(Build)/Merged_section.o: src/Merged_section.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

$(Build)/Output_chunk.o: src/Output_chunk.cpp
	$(CC) $< $(CPP_FLAG) $(INCLUDE) -c -o $@

ld: $(OBJS)
	$(CC) $^ $(CPP_FLAG) -o $@

gdb_ld: $(OBJS)
	$(CC) $^ $(CPP_FLAG) -o $@
	cp $@ third_proj/a-b-tree

run_example: ld
	valgrind --leak-check=full ./ld $(link_test_args)

linking: ld
	./ld $(link_test_args)

run_gdb: gdb_ld
#	gdb --args /usr/local/bin/mold $(link_test_args) --no-fork
	gdb --args ./gdb_ld  $(link_test_args)
	
clean:
	$(shell rm -rf ./test3.elf)
	$(shell rm -rf $(OBJS))
	$(shell rm -rf $(DEPS))
	$(shell rm -rf $(BINS))

-include $(DEPS)