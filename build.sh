#!/bin/bash
# ============================================================
# Arfuso OS v1.3.0 - Script de construction complet
# Produit : dist/arfuso-1.3.1-x86_64.iso (bootable BIOS / Vectras VM)
# ============================================================
set -e
cd "$(dirname "$0")"
mkdir -p build dist iso_root

CFLAGS="-ffreestanding -m64 -mcmodel=large -mno-red-zone -nostdlib -fno-pie \
        -fno-stack-protector -fno-exceptions -O2 -Wall -I./kernel"

echo "[1/7] Génération des assets (polices, logo, couleurs, images)..."
python3 tools/gen_assets.py
python3 tools/gen_images.py

gen_mbr_h() {
    python3 - "$1" <<'EOF'
import sys
data = open(sys.argv[1], 'rb').read()
assert len(data) == 512, "MBR doit faire 512 octets"
with open('kernel/generated/mbr_image.h', 'w') as f:
    f.write('// AUTO-GENERATED\n#pragma once\nstatic const unsigned char mbr_image[512] = {\n')
    for i in range(0, 512, 16):
        f.write('    ' + ','.join(str(b) for b in data[i:i+16]) + ',\n')
    f.write('};\n')
EOF
}

assemble_kernel() {
    KS=$1
    echo "      -> noyau avec $KS secteurs"
    nasm -f bin -D KSECTORS=$KS boot/boot.asm -o build/mbr.bin
    gen_mbr_h build/mbr.bin
    nasm -f elf64 kernel/entry.asm -o build/entry.o
    for f in fb kbd mouse rtc ata usbtab net fs bmp apps installer kernel; do
        gcc $CFLAGS -DKSECTORS_VAL=$KS -c kernel/$f.c -o build/$f.o
    done
    ld -nostdlib -T kernel/kernel.ld -o build/kernel.elf \
        build/entry.o build/fb.o build/kbd.o build/mouse.o build/rtc.o build/ata.o build/usbtab.o build/net.o build/fs.o build/bmp.o build/apps.o build/installer.o build/kernel.o
    objcopy -O binary build/kernel.elf build/kernel.bin
}

echo "[2/7] Passe 1 : mesure du noyau..."
assemble_kernel 1
KS=$(( ( $(stat -c%s build/kernel.bin) + 511 ) / 512 ))
RAW_SIZE=$(stat -c%s build/kernel.bin)
echo "      Taille du noyau : $RAW_SIZE octets -> $KS secteurs"
MAX=$((620*1024))
if [ "$RAW_SIZE" -gt "$MAX" ]; then
    echo "ERREUR : noyau trop gros (max $MAX)"; exit 1
fi

echo "[3/7] Passe 2 : reconstruction finale (chargeur patché)..."
assemble_kernel $KS

echo "[4/7] Image de boot amorçable (MBR + noyau)..."
cat build/mbr.bin build/kernel.bin > build/arfubo.bin
TOTAL_SIZE=$(stat -c%s build/arfubo.bin)
LOAD_SECTORS=$(( (TOTAL_SIZE + 511) / 512 ))
echo "      Image totale : $TOTAL_SIZE octets ($LOAD_SECTORS secteurs)"

echo "[5/7] Préparation du contenu de l'ISO..."
cp build/arfubo.bin iso_root/
cp assets/logo.png iso_root/logo-arfuso.png
cat > iso_root/LISEZMOI.TXT <<EOF
Arfuso OS v1.3.0 - noyau 100% maison (aucune base Linux/Windows)

IMPORTANT : Arfuso ne fonctionne qu'APRES installation sur un disque.
1. Demarrez la machine sur cet ISO (CD-ROM) : l'assistant s'ouvre.
2. Choisissez langue / clavier / fuseau horaire, puis votre edition,
   la plateforme et le disque d'installation (tout a la souris).
3. Redemarrez sur le disque dur : le systeme complet se lance.

- Souris : usb-tablet (pointeur absolu) ou PS/2 ; curseur sur TOUS les ecrans
- Reseau : pile TCP/IP maison (e1000, DHCP, ARP, DNS), navigateur Chrome Arfuso (HTTP)
- Fichiers : AFS (maison), explorateur avec copier/coller, lecture ISO9660 (CD) et FAT32 (cle USB)
- Applications .arf dans Applications/ ; Arfu Store pour en installer
- Barre des taches : les applis ouvertes apparaissent dans le dock a droite
- Mot de passe de demo : arfuso
- Touches : 1/2/3/4 applis, V verrouiller, P eteindre
EOF

echo "[6/7] Création de l'ISO El Torito..."
xorriso -as mkisofs -quiet \
    -o dist/arfuso-1.3.1-x86_64.iso \
    -V "ARFUSO_1_3_0" -J -R \
    -b arfubo.bin \
    -no-emul-boot \
    -boot-load-size $LOAD_SECTORS \
    -c boot.catalog \
    iso_root

echo "[7/7] Terminé !"
ls -la dist/
echo ""
echo "===> dist/arfuso-1.3.1-x86_64.iso"
