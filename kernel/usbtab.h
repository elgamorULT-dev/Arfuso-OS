#pragma once
#include "sys.h"

/* usb-tablet QEMU (pointeur absolu) via UHCI */
int  usb_tablet_init(void);
int  usb_tablet_available(void);
int  usb_tablet_poll(int *ax, int *ay);

/* Cle USB (stockage de masse) */
int  usb_msc_available(void);
int  usb_msc_read(u32 lba512, u8 *dst, int nsecs);   /* max 16 secteurs */
