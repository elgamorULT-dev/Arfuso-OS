/*
 * Arfuso OS v1.0.0 - Noyau ARM64 (console série)
 * Base de la plateforme mobile/ARM du projet : même noyau maison,
 * sans aucune base Linux. La version graphique ARM64 (écrans Arfuso
 * sur framebuffer ramfb/virtio-gpu + tactile) est prévue en v1.1.
 */
#include <stdint.h>

#define UART0_BASE 0x09000000UL          /* PL011 (machine virt de QEMU) */
#define UART_DR    (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR    (*(volatile uint32_t *)(UART0_BASE + 0x18))

static void uart_putc(char c) {
    while (UART_FR & (1u << 5)) {}       /* attente FIFO émission libre */
    UART_DR = (uint32_t)c;
}
static int uart_getc(void) {
    if (UART_FR & (1u << 4)) return -1;  /* FIFO réception vide */
    return (int)(UART_DR & 0xFF);
}
static void uart_str(const char *s) {
    while (*s) { if (*s == '\n') uart_putc('\r'); uart_putc(*s++); }
}

static const char *BANNER =
    "\n"
    "  ============================================\n"
    "    Arfuso OS v1.0.0  -  Noyau ARM64 (maison)\n"
    "  ============================================\n\n"
    "  Plateforme : ARM64 / AArch64\n"
    "  Editions   : Professionnelle, Familiale, Education, Gaming\n"
    "  Statut     : console serie OK - interface graphique ARM64 en v1.1\n\n"
    "  Tapez des touches : le noyau les reaffiche (Echo Arfuso).\n"
    "  > ";

void kmain(void) {
    uart_str(BANNER);
    for (;;) {
        int c = uart_getc();
        if (c >= 0) {
            if (c == '\r') uart_str("\n  > ");
            else uart_putc((char)c);
        }
        __asm__ volatile("wfe");
    }
}
