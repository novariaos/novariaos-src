RV_TOOLCHAIN ?= /home/zennix/xpack-riscv-none-elf-gcc-13.2.0-2
CROSS := $(RV_TOOLCHAIN)/bin/riscv-none-elf-

CC := $(CROSS)gcc

ARCH := -march=rv64i -mabi=lp64 -mcmodel=medlow
CFLAGS := $(ARCH) -ffreestanding -fno-pic -fno-pie -msmall-data-limit=0 -O2 -Wall -I nvlibc/include
LDFLAGS := $(ARCH) -nostdlib -T nvlibc/novaria.ld
LDLIBS := nvlibc/crt0.o nvlibc/libnvlibc.a -lgcc