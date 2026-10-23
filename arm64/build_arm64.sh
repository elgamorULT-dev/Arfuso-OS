#!/bin/bash
# Arfuso OS - Construction du noyau ARM64 (kernel8.img bootable via QEMU -kernel)
set -e
cd "$(dirname "$0")"
CROSS=aarch64-linux-gnu
CFLAGS="-ffreestanding -nostdlib -fno-pie -fno-stack-protector -O2 -Wall -mgeneral-regs-only"

echo "[ARM64] Assemblage du point d'entrée..."
$CROSS-gcc $CFLAGS -c start.S -o ../build_arm64_start.o
echo "[ARM64] Compilation du noyau..."
$CROSS-gcc $CFLAGS -c kernel.c -o ../build_arm64_kernel.o
echo "[ARM64] Édition de liens..."
$CROSS-ld -nostdlib -T link.ld -o ../dist/arfuso-1.2.0-arm64.elf \
    ../build_arm64_start.o ../build_arm64_kernel.o
$CROSS-objcopy -O binary ../dist/arfuso-1.2.0-arm64.elf ../dist/arfuso-1.2.0-arm64-kernel8.img
rm -f ../build_arm64_start.o ../build_arm64_kernel.o
echo "[ARM64] Terminé :"
ls -la ../dist/arfuso-1.2.0-arm64*
echo ""
echo "Test : qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 256 \\"
echo "       -kernel dist/arfuso-1.2.0-arm64-kernel8.img -nographic"
