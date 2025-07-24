; kernel.asm (부트로더가 0x1000에 로드한다고 가정)
[bits 16]
[org 0x1000]

start:
    cli                    ; 1) 인터럽트 먼저 OFF
    call enable_a20        ; 2) call로 A20 켜고 복귀
    lgdt  [gdt_descriptor] ; 3) GDT 로드

    ; 4) Protected Mode 진입
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_start      ; Far jump 로 파이프라인 플러시

; ---------- Real‑mode 루틴 ----------
enable_a20:
    in   al, 0x92
    or   al, 00000010b     ; A20 enable
    out  0x92, al
    ret

gdt_start:
    dq 0x0000000000000000          ; Null
    dq 0x00CF9A000000FFFF          ; Code 0x08
    dq 0x00CF92000000FFFF          ; Data 0x10
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------- 32‑bit 영역 ----------
[bits 32]
pm_start:
    cli                           ; 아직 IDT 없으니 계속 OFF
    mov ax, 0x10                  ; Data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00              ; 간단히 0x9FC00 쪽에 스택

    call kernel_main
.hang: hlt
       jmp .hang

kernel_main:
    mov esi, msg
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    ret

msg db "Hello from Protected Mode!",0

times 10240-($-$$) db 0

