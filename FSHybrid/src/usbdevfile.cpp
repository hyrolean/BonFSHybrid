/* SunPTV-USB   (c) 2016 trinity19683
  finding USB device file (MS-Windows)
  usbdevfile.cpp
  2016-01-23
*/

#include "stdafx.h"
#include <setupapi.h>
#include <strsafe.h>
#include <WinUSB.h>

#include "usbdevfile.h"

// Driver Instance GUID
DEFINE_GUID( GUID_WINUSB_DRV,	0xa70cc802, 0x7309, 0x486d, 0xbe, 0xe8, 0x93, 0xa0, 0x48, 0xcf, 0x6c, 0x63);

HANDLE usbdevfile_alloc(unsigned int * const idx)
{
	DWORD dwRet;
	ULONG length;
	HANDLE hDev = INVALID_HANDLE_VALUE;
	GUID * const pDrvID = (GUID *)&GUID_WINUSB_DRV;

	// get handle to device info.
	HDEVINFO deviceInfo = SetupDiGetClassDevs(pDrvID, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if(INVALID_HANDLE_VALUE == deviceInfo) return NULL;

	SP_DEVICE_INTERFACE_DATA interfaceData;
	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for(; *idx < 40; (*idx)++ ) {
		//# enumerate device interfaces
		if( FALSE == SetupDiEnumDeviceInterfaces(deviceInfo, NULL, pDrvID, *idx, &interfaceData) ) {
			dwRet = GetLastError();
			//if(dwRet == ERROR_NO_MORE_ITEMS) break;
			break;
		}

		ULONG requiredLength = 0;
		SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredLength, NULL);
		//# allocate buffer
		requiredLength += sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA) + sizeof(TCHAR);
		PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)GlobalAlloc(GMEM_FIXED, requiredLength);
		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		//#  get path name of a device
		length = requiredLength;
		if(SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData, length, &requiredLength, NULL) ) {
			//# success
			hDev = CreateFile(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if( hDev == INVALID_HANDLE_VALUE ) {
				//dwRet = GetLastError();
				//if (dwRet == ERROR_ACCESS_DENIED)  //# using
			}else{
				GlobalFree(detailData);
				break;
			}
		}
		GlobalFree(detailData);
	}
	SetupDiDestroyDeviceInfoList(deviceInfo);
	
	if(INVALID_HANDLE_VALUE == hDev) return NULL;
	return hDev;
}

HANDLE usbdevfile_init(HANDLE hDev)
{
	HANDLE usbHandle;
	if(FALSE == WinUsb_Initialize( hDev, &usbHandle ))
		return NULL;
	return usbHandle;
}

void usbdevfile_free(HANDLE usbHandle)
{
	WinUsb_Free( usbHandle );
}

/*EOF*/