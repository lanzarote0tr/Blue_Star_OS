all: kernel.asm bootloader.asm
	nasm -f bin kernel.asm -o kernel.bin
	nasm -f bin bootloader.asm -o bootloader.bin
	cat bootloader.bin kernel.bin > os.img
run: os.img
	qemu-system-i386 -nographic -drive format=raw,file=os.img,if=floppy

