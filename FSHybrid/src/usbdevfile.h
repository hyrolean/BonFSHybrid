/* fsusb2i   (c) 2015 trinity19683
  finding USB device file (MS-Windows)
  usbdevfile.h
  2015-12-10
*/
#pragma once

// Driver Instance GUID
DEFINE_GUID( GUID_WINUSB_FSUSB2N_DRV,	0xb35924d6, 0x3e09, 0x4a9e, 0x97, 0x82, 0x55, 0x24, 0xa4, 0xb7, 0x9b, 0xa4 );
DEFINE_GUID( GUID_WINUSB_FSUSB2V3_DRV,	0x77ed26ec, 0x2783, 0x7bba, 0xa8, 0x24, 0x00, 0xbc, 0xad, 0x7a, 0xcd, 0xb9 );
DEFINE_GUID( GUID_WINUSB_US3POUT_DRV,	0xa70cc802, 0x7309, 0x486d, 0xbe, 0xe8, 0x93, 0xa0, 0x48, 0xcf, 0x6c, 0x63 );

HANDLE usbdevfile_alloc(int * const idx, const GUID *pDrvID);
HANDLE usbdevfile_init(HANDLE hDev);
void usbdevfile_free(HANDLE usbHandle);

/*EOF*/