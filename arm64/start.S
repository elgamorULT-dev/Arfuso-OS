// Arfuso OS ARM64 - Point d'entrée du noyau (charge par QEMU -kernel)
.section .text
.global _start
_start:
    ldr x0, =_stack_top
    mov sp, x0
    // Mise à zéro du .bss
    ldr x0, =_bss_start
    ldr x1, =_bss_end
1:  cmp x0, x1
    b.hs 2f
    str xzr, [x0], #8
    b 1b
2:  bl kmain
3:  wfe
    b 3b

.section .bss
.align 4
_stack:
    .skip 16384
_stack_top:
