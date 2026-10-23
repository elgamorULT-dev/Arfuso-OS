; ============================================================
; Arfuso OS - Point d'entrée du noyau (placé à 0x100000)
; Le stub 32 bits construit la pagination (identité 0 -> 4 Go),
; active le mode 64 bits (long mode), puis saute au code 64 bits.
; ============================================================

global _start
extern kmain
extern _bss_start
extern _bss_end

section .text

BITS 32
_start:
    ; Activer SSE (utilisé par le code C 64 bits)
    mov eax, cr4
    or eax, 0x600
    mov cr4, eax
    mov eax, cr0
    and eax, ~4
    or eax, 2
    mov cr0, eax

    ; Tables de pagination : PML4 = 0x70000, PDPT = 0x71000,
    ; PD = 0x72000..0x75FFF (identité 0 -> 4 Go, pages 2 Mo)
    mov edi, 0x70000
    xor eax, eax
    mov ecx, 0x1800
    rep stosd
    mov dword [0x70000], 0x71003
    mov dword [0x71000], 0x72003
    mov dword [0x71008], 0x73003
    mov dword [0x71010], 0x74003
    mov dword [0x71018], 0x75003
    mov edi, 0x72000
    mov eax, 0x83
    mov ecx, 2048
.fillpd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .fillpd

    ; PAE
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    ; CR3 -> PML4
    mov eax, 0x70000
    mov cr3, eax

    ; LME (Long Mode Enable)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Activer la pagination -> mode 64 bits effectif après le saut
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    jmp 0x18:krn64

BITS 64
krn64:
    mov ax, 0x20
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov rsp, 0x6FF00                  ; pile sous les tables de pagination

    ; Mise à zéro de la section .bss
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    shr rcx, 3
    xor rax, rax
    rep stosq

    call kmain

.halt:
    cli
    hlt
    jmp .halt
