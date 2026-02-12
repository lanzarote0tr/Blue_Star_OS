# Toolchain
CC      = gcc
LD      = ld
OBJCOPY = objcopy

# Flags
CFLAGS  = -ffreestanding -fno-stack-protector -fPIC -fPIE \
          -m64 -mno-shstk -O2
LDFLAGS = -nostdlib -z max-page-size=0x1000 -T linker.ld

# Files
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, build/%.o, $(SRCS))

ELF  = build/kernel.elf
BIN  = build/kernel

# Rules
all: clean $(BIN)

build:
	mkdir -p build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJS) $(ELF) $(BIN)

.PHONY: all clean
