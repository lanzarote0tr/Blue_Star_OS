; bootloader.asm
[bits 16]
[org 0x7C00]

start:
    mov ax, 0x100
    mov es, ax
    xor bx, bx

    mov ah, 0x02     ; BIOS interrupt: read sectors
    mov al, 20       ; Number of sectors to read
    mov ch, 0        ; Cylinder
    mov cl, 2        ; Sector (starts from 1, kernel from 2)
    mov dh, 0        ; Head
    mov dl, 0x00     ; Drive (floppy disk)

    int 0x13         ; BIOS read disk

    jc start         ; Carry flag → When error

    jmp 0x1000       ; kernel

disk_error:
    mov si, err
.errloop:
    lodsb
    or al, al
    jz $
    mov ah, 0x0E
    int 0x10
    jmp .errloop

err: db 'Disk read error', 0

times 510 - ($ - $$) db 0
dw 0xAA55
