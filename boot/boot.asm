; ============================================================
; Arfuso OS v1.0.0 - Chargeur d'amorçage (MBR / El Torito)
; 100 % maison.
; Rôle : charger le noyau, choisir le mode graphique VESA,
; passer en mode protégé 32 bits, copier le noyau à 1 Mo
; et sauter dedans. Le noyau prend ensuite le relais
; (pagination + mode 64 bits : voir kernel/entry.asm).
; ============================================================

BITS 16
ORG 0x7C00

%ifndef KSECTORS
%define KSECTORS 1
%endif

start:
    jmp short boot
    nop
    times 0x40 - ($ - $$) db 0        ; zone réservée (table El Torito éventuelle)
ksectors: dd KSECTORS                 ; secteurs 512o du noyau (patché au build)

; Mémoire basse utilisée :
;   0x0500-0x050F : paquet EDD     0x05F2 : secteurs restants
;   0x05F4 : secteurs chargés      0x05F8 : lecteur de boot
;   0x0600-0x06FF : infos du mode VESA

boot:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [0x05F8], dl

    ; ---- Depuis un disque (DL < 0xE0) : charger le noyau via BIOS ----
    ; (Sur ISO, le BIOS El Torito a déjà chargé tout le bloc en 0x7C00+.)
    cmp dl, 0xE0
    jae .kernel_ok
    mov ah, 0x41                      ; extensions BIOS présentes ?
    mov bx, 0x55AA
    int 0x13
    jc .kernel_ok
    cmp bx, 0xAA55
    jne .kernel_ok

    mov ax, [ksectors]
    mov [0x05F2], ax
    mov word [0x05F4], 0
.load_loop:
    mov ax, [0x05F2]
    test ax, ax
    jz .kernel_ok
    cmp ax, 64
    jbe .reste
    mov ax, 64
.reste:
    mov byte  [0x0500], 16
    mov byte  [0x0501], 0
    mov [0x0502], ax                  ; nombre de secteurs
    mov word  [0x0504], 0             ; offset
    mov bx, [0x05F4]
    shl bx, 5
    add bx, 0x07E0
    mov [0x0506], bx                  ; segment = 0x07E0 + déjà_lus*32
    mov bx, [0x05F4]
    inc bx
    mov [0x0508], bx                  ; LBA64 = 1 + déjà_lus
    mov word [0x050A], 0
    mov word [0x050C], 0
    mov word [0x050E], 0
    mov cl, al
    mov ah, 0x42
    mov dl, [0x05F8]
    mov si, 0x0500
    int 0x13
    jc .disk_error
    movzx ax, cl
    add [0x05F4], ax
    sub [0x05F2], ax
    jmp .load_loop

.disk_error:
    mov si, msg_err
    call print16
.hang:
    cli
    hlt
    jmp .hang

.kernel_ok:
    ; ---- Mode graphique VESA (32 bits/pixel) ----
    mov cx, 0x4118                    ; 1024x768
    call vesa_try
    jc .video_ok
    mov cx, 0x4115                    ; 800x600
    call vesa_try
    jc .video_ok
    mov cx, 0x4112                    ; 640x480
    call vesa_try
    jc .video_ok
    mov si, msg_vesa
    call print16
.hang2:
    cli
    hlt
    jmp .hang2

.video_ok:
    ; ---- Ligne A20 ----
    in al, 0x92
    or al, 2
    out 0x92, al

    ; ---- Mode protégé 32 bits ----
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pm32

print16:
    lodsb
    test al, al
    jz .fin
    mov ah, 0x0E
    int 0x10
    jmp print16
.fin:
    ret

; Essayer un mode VESA (CX = mode). Infos placées en 0x0600. Carry = succès.
vesa_try:
    pusha
    mov di, 0x0600
    push cx
    mov cx, 128
    xor al, al
.z: mov [di], al
    inc di
    loop .z
    pop cx
    mov ax, 0x4F01
    mov di, 0x0600
    xor bx, bx
    mov es, bx
    int 0x10
    cmp ax, 0x004F
    jne .ko
    mov ax, 0x4F02
    mov bx, cx
    int 0x10
    cmp ax, 0x004F
    jne .ko
    popa
    stc
    ret
.ko:
    popa
    clc
    ret

msg_err:  db "Erreur disque", 0
msg_vesa: db "Erreur video", 0

BITS 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x6FFF0

    ; Copier le noyau de 0x7E00 vers 0x100000 (1 Mo)
    mov esi, 0x7E00
    mov edi, 0x100000
    mov ecx, [0x7C00 + 0x40]
    shl ecx, 7                        ; secteurs * 128 double-mots
    rep movsd

    jmp 0x08:0x100000                 ; saut dans le noyau (stub 32 bits)

align 8
gdt:
    dq 0x0000000000000000             ; 0x00 nul
    dq 0x00CF9A000000FFFF             ; 0x08 code 32 bits
    dq 0x00CF92000000FFFF             ; 0x10 données 32 bits
    dq 0x00209A0000000000             ; 0x18 code 64 bits
    dq 0x0000920000000000             ; 0x20 données 64 bits
gdt_end:
gdtr:
    dw gdt_end - gdt - 1
    dd gdt

    times 510 - ($ - $$) db 0
    dw 0xAA55
