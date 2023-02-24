/* SunPTV-USB   (c) 2016 trinity19683
  EM287x USB commands (MS-Windows)
  em287x_usb.c
  2016-01-22
*/

#include "stdafx.h"
#include <errno.h>
#include <WinUSB.h>

#include "osdepend.h"
#include "em287x_usb.h"
#include "em287x_priv.h"

int em287x_ctrl(HANDLE fd, const uint16_t reg, const uint16_t size, uint8_t* const data, const unsigned mode)
{
	int ret;
	WINUSB_SETUP_PACKET spkt;
	
	ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = (mode & 0x1) ? 0x40 : 0xc0;
	spkt.Request = (mode >> 8) & 0xFF;
	spkt.Index = reg;
	spkt.Length = size;
	if(! WinUsb_ControlTransfer ( fd, spkt, (PUCHAR)data, size, (PULONG)&ret, NULL ) ) {
		ret = GetLastError();
		warn_info(ret,"%02X_%02X", (mode >> 8) & 0xFF, reg);
		return ret;
	}
	return 0;
}

/*EOF*/