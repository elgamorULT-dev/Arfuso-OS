# Arfuso OS — v1.3.1

Système d'exploitation indépendant écrit **100 % from scratch** (aucune base
Linux, Windows ou autre) : chargeur de démarrage (bootloader), noyau 64 bits,
pilotes matériels, pile réseau TCP/IP et interface graphique entièrement faits
maison, en assembleur (NASM) et en C.

## Livrables (dossier `releases/`)

| Fichier | Cible |
|---|---|
| `arfuso-1.3.1-x86_64.iso` | PC x86_64 (BIOS), ISO amorçable = assistant d'installation (**dernière version**) |
| `arfuso-1.0.0 / 1.1.0 / 1.2.0 …` | versions précédentes conservées |
| `arfuso-1.x.0-arm64-kernel8.img` | ARM64 (noyau de démarrage minimal, sortie série) |

L'ISO installe Arfuso OS sur le disque dur. Au premier démarrage, un petit
assistant crée votre profil (identifiant + mot de passe optionnel), puis
l'appareil redémarre tout seul : écran de démarrage image, écran de
verrouillage image, écran de connexion, bureau.

## Nouveautés v1.3.1 — Paramètres complets

L'application **Paramètres** couvre maintenant 9 sections, dans le style des
grandes plateformes :

1. **Système** — affichage (échelle 100 %/125 %), etendue d'ecran simulee,
   modes d'alimentation (économie / équilibré / performance), minuteur de
   mise en veille, stockage visuel (barre d'occupation AFS).
2. **Réseau** — état IP passerelle/DNS réels de la pile maison, relance DHCP,
   **mode avion** (coupe réellement le navigateur et le Store), Bluetooth.
3. **Personnalisation** — thème sombre/clair (enregistré), **couleur
   d'accentuation appliquée à toute l'interface** (6 couleurs), fond
   d'écran : couleur unie ou **image BMP du dossier Images** (+ import
   depuis CD/USB), taille des textes.
4. **Applications** — liste réelle du dossier Applications avec tailles,
   **désinstallation des apps .arf tierces**, associations par défaut.
5. **Comptes** — comptes utilisateurs réels (création id/mot de passe),
   option d'exigence du mot de passe au démarrage.
6. **Sécurité** — pare-feu (état réel de la pile TCP), permissions
   caméra/micro/localisation/fichiers, chiffrement disque, télémétrie.
7. **Accessibilité** — contraste élevé, état des aides (loupe, filtres,
   clavier visuel…).
8. **Mise à jour & récupération** — historique des versions, **bouton
   Réinitialiser** (relance l'assistant de premier démarrage, profil effacé),
   redémarrer / éteindre / verrouiller.
9. **À propos** — CPU réel (CPUID), **RAM réelle** (registres CMOS BIOS),
   framebuffer, occupation disque, version/build/architecture.

Tout ce que le noyau peut réellement faire est branché pour de vrai ; le
reste est clairement étiqueté « non disponible en v1.3 ». Réglages
persistants (secteur de configuration sur disque).

## Nouveautés v1.3

- **Navigateur web réel** (« Chrome Arfuso ») : pile réseau TCP/IP maison
  (pilote e1000, DHCP, ARP, DNS, TCP, HTTP) — charge de vraies pages HTTP
  (testé : `http://info.cern.ch/` → HTTP 200).
- **Premier démarrage guidé** : création du profil utilisateur
  (identifiant + mot de passe optionnel), configuration, puis
  **redémarrage automatique** avec barre de progression.
- **Nouvel écran de connexion** : deux champs (identifiant / mot de passe),
  identifiant pré-rempli automatiquement.
- **Écrans de démarrage et de verrouillage** : image plein écran, sans texte.
- **Explorateur de fichiers**, mini-applications **.arf**, **Arfu Store**
  (installation d'apps .arf, y compris depuis une URL http://), **ArfuNotes**
  (.txt), Terminal, Snake, Paramètres.
- **Dock = barre des tâches** à droite (applications réduites/relancées),
  top-bar cliquable, fenêtres arrondies.
- Curseur partout (usb-tablet, position absolue, repli PS/2).

## Architecture du dépôt

```
boot/                 bootloader (NASM, 16 → 64 bits)
kernel/               noyau : framebuffer, ATA, USB UHCI + usb-tablet, PS/2,
                      RTC, système de fichiers AFS maison, pile réseau,
                      interface graphique, applications intégrées
kernel/generated/     en-têtes générés (polices, images, MBR)
arm64/                variante ARM64 (noyau minimal boot + série)
assets/               images et polices du système
tools/                scripts de test (QEMU/QMP)
build.sh              compilation complète → dist/arfuso-<version>-x86_64.iso
releases/             ISOs livrées de chaque version
```

## Compilation (x86_64)

Prérequis : `nasm`, `xorriso`, `mtools`, `qemu-system-x86`, `gcc`, `python3`

```bash
bash build.sh        # produit dist/arfuso-1.3.0-x86_64.iso
```

## Essai rapide (QEMU)

```bash
qemu-img create -f raw disk.img 64M
qemu-system-x86_64 -machine pc -m 256 \
  -drive file=disk.img,if=ide,index=0,media=disk,format=raw \
  -drive file=releases/arfuso-1.3.0-x86_64.iso,if=ide,index=2,media=cdrom,readonly=on \
  -boot d -usb -device usb-tablet -netdev user,id=n0 -device e1000,netdev=n0
```

Suivre l'assistant (100 % à la souris), puis **retirer le CD** (ou `-boot c`)
au redémarrage. La carte `e1000` + `-netdev user` fournissent le réseau au
navigateur (DHCP automatique). Fonctionne aussi sous **Vectras VM** (ARM64)
avec les mêmes réglages (carte e1000 en NAT).

## Historique

- **v1.3** — navigateur réel + pile réseau, premier démarrage guidé, nouveaux
  écrans (démarrage / verrouillage / connexion), explorateur, .arf, Arfu Store…
- **v1.2** — usb-tablet (curseur exact), installateur 100 % souris, refus de
  démarrer depuis le CD sur un PC déjà installé, configuration persistante.
- **v1.1 / v1.0** — premières versions : noyau, framebuffer, installateur.
