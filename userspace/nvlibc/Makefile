RV_TOOLCHAIN ?= /home/zennix/xpack-riscv-none-elf-gcc-13.2.0-2
CROSS := $(RV_TOOLCHAIN)/bin/riscv-none-elf-

CC := $(CROSS)gcc
AR := $(CROSS)ar

ARCH := -march=rv64i -mabi=lp64 -mcmodel=medlow
CFLAGS := $(ARCH) -ffreestanding -fno-builtin -fno-pic -fno-pie -msmall-data-limit=0 -O2 -Wall -I./include
ASFLAGS := $(ARCH)

all: crt0.o libnvlibc.a

crt0.o: crt0.S
	$(CC) $(ASFLAGS) -c $< -o $@

syscall.o: syscall.S
	$(CC) $(ASFLAGS) -c $< -o $@

libnvlibc.a: syscall.o exit.o io.o string.o mem.o
	$(AR) rcs $@ $^

%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a