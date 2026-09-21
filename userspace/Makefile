include rules.mk

OUT := build
ROOTFS_BIN := ../rootfs/bin

PROGRAMS := hello calc pyramid cat

BINS := $(addprefix $(OUT)/,$(addsuffix .bin,$(PROGRAMS)))

all: nvlibc $(BINS)

nvlibc:
	$(MAKE) -C nvlibc

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.bin: programs/%.c nvlibc/crt0.o nvlibc/libnvlibc.a | $(OUT)
	$(CC) $(CFLAGS) $< $(LDLIBS) $(LDFLAGS) -o $@
	cp $@ $(ROOTFS_BIN)/$*.bin

clean:
	rm -rf $(OUT)
	$(MAKE) -C nvlibc clean
	rm -f $(ROOTFS_BIN)/$(PROGRAMS:%=%.bin)

.PHONY: all nvlibc clean