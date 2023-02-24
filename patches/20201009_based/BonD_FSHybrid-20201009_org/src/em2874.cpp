#include "stdafx.h"
#include "em2874.h"
#include "../twindbg.h"
#include <setupapi.h>
#include <strsafe.h>

// ドライバインスタンスのGUID
DEFINE_GUID( GUID_WINUSB_DRIVER,	0xb35924d6L, 0x3e09, 0x4a9e, 0x97, 0x82, 0x55, 0x24, 0xa4, 0xb7, 0x9b, 0xa4 );

inline uint8_t ICC_checkSum (const uint8_t* data, int len)
{
	uint8_t sum = 0;
	for ( ; len > 0; len-- ) {
		sum ^= *data++;
	}
	return sum;
}

inline void miliWait( int s )
{
	::Sleep(s);
}

unsigned EM2874Device::UserSettings = 0x1;

EM2874Device::EM2874Device ()
: dev(NULL), usbHandle(NULL), isCardReady(false)
#ifdef EM2874_TS
, hTsEvent(NULL), TsBuffSize(NULL)
#endif
{
}

EM2874Device::EM2874Device(HANDLE hDev)
: dev(hDev), usbHandle(NULL), isCardReady(false)
#ifdef EM2874_TS
, hTsEvent(NULL), TsBuffSize(NULL)
#endif
{
}

EM2874Device::~EM2874Device ()
{
	if (hTsEvent) {
		::CloseHandle(hTsEvent);
		hTsEvent = NULL;
	}
	if ( usbHandle ) {
		writeReg(EM2874_REG_TS_ENABLE, 0);
		::WinUsb_SetCurrentAlternateSetting( usbHandle, 0 );
		if(this->UserSettings & 0x1)
		{
			// デバイスの電源をOFF
			writeReg(EM28XX_REG_GPIO, readReg(EM28XX_REG_GPIO) | 0x1);
			writeReg(EM2874_REG_CAS_MODE1, 0x0);
			writeReg(0x0C, 0x0);
		}
		::WinUsb_Free( usbHandle );
	}
	if ( this->dev ) {
		::CloseHandle( this->dev );
	}
}

EM2874Device* EM2874Device::AllocDevice(int &idx)
{
	DWORD dwRet;
	ULONG length;
	
	HANDLE hDev = INVALID_HANDLE_VALUE;

	// デバイス情報セットのハンドル取得
	HDEVINFO deviceInfo = SetupDiGetClassDevs((GUID *)&GUID_WINUSB_DRIVER, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if(deviceInfo == INVALID_HANDLE_VALUE) return NULL;

	SP_DEVICE_INTERFACE_DATA interfaceData;
	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for(; idx < 20; idx++ ) {
		// デバイスインタフェースを列挙
		if( FALSE == SetupDiEnumDeviceInterfaces(deviceInfo, NULL, (GUID *)&GUID_WINUSB_DRIVER, idx, &interfaceData) ) {
			dwRet = ::GetLastError();
			//if(dwRet == ERROR_NO_MORE_ITEMS) break;
			break;
		}

		ULONG requiredLength = 0;
		SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredLength, NULL);
		// バッファを確保
		requiredLength += sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA) + sizeof(TCHAR);
		PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)GlobalAlloc(GMEM_FIXED, requiredLength);
		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		// デバイスのパス名を取得
		length = requiredLength;
		if(SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData, length, &requiredLength, NULL) ) {
			// path取得成功
			hDev = ::CreateFile(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if( hDev == INVALID_HANDLE_VALUE ) {
				//dwRet = ::GetLastError();
				//if (dwRet == ERROR_ACCESS_DENIED) 使用中
			}else{
				GlobalFree(detailData);
				break;
			}
		}
		GlobalFree(detailData);
	}
	SetupDiDestroyDeviceInfoList(deviceInfo);

	if (hDev == INVALID_HANDLE_VALUE) return NULL;

	EM2874Device *pDev = new EM2874Device();
	pDev->dev = hDev;
	if(! pDev->initDevice()) {
		delete pDev;
		return NULL;
	}
	return pDev;
}

bool EM2874Device::initDevice ()
{
	uint8_t val;

	if(FALSE == ::WinUsb_Initialize ( this->dev, &usbHandle ))
		return false;
	if( readReg(EM28XX_REG_GPIO, &val) && writeReg(EM28XX_REG_GPIO, val & ~0x1U)
	){
		{
			TCHAR szBuff[40];
			_stprintf_s(szBuff, 40, TEXT("R80@init=%02X\n"), val);
			::OutputDebugString(szBuff);
		}

#ifdef _DEBUG
		ULONG ulVal;
		ULONG ulLen = sizeof(ULONG);
		::WinUsb_GetPipePolicy( usbHandle, EM2874_EP_TS1, MAXIMUM_TRANSFER_SIZE, &ulLen, &ulVal);
		DBG_INFO("MAXIMUM_TRANSFER_SIZE=%u\n",ulVal);
#endif
	}else{
		return false;
	}
	if(writeReg(0x0C, 0x10)
	&& writeReg(0x12, 0x27) )
	{
		writeReg(0x13, 0x10);
		writeReg(0x10, 0);
		writeReg(0x11, 0x11);
		writeReg(0x28, 0x01);
		writeReg(0x29, 0xff);
		writeReg(0x2a, 0x01);
		writeReg(0x2b, 0xff);
		writeReg(0x1c, 0);
		writeReg(0x1d, 0);
		writeReg(0x1e, 0);
		writeReg(0x1f, 0);
		writeReg(0x1b, 0);
		writeReg(0x5e, 128);
		writeReg( EM2874_REG_TS_ENABLE, 0 );
		return true;
	}

	return false;
}

bool EM2874Device::initDevice2 ()
{
	uint8_t val;
	readReg(EM28XX_REG_GPIO, &val);
	writeReg( EM28XX_REG_GPIO, ~0xc1U & val );
	
	miliWait(70);
	readReg(EM28XX_REG_GPIO, &val);
	writeReg( EM28XX_REG_GPIO, 0x40U | val );
	readReg(EM28XX_REG_GPIO, &val);
	writeReg( EM28XX_REG_GPIO, 0x7eU | val );
	miliWait(3);
	::WinUsb_SetCurrentAlternateSetting( usbHandle, 1 );

	return true;
}

uint8_t EM2874Device::readReg (const uint8_t idx)
{
	uint8_t val;
	readReg (idx, &val);
	return val;
}

bool EM2874Device::readReg (const uint8_t idx, uint8_t *val)
{
	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0xc0;
	spkt.Index = idx;
	spkt.Length = 1;

	ULONG ret;
	BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, (PUCHAR)val, 1, &ret, NULL );
	if( !bRet ) {
		DBG_INFO ( "readReg LastError=%08x\n", GetLastError() );
	}
	return (ret == 1) && bRet;
}

bool EM2874Device::writeReg (const uint8_t idx, const uint8_t val)
{
	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0x40;
	spkt.Index = idx;
	spkt.Length = 1;

	ULONG ret;
	BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, (PUCHAR)&val, 1, &ret, NULL );
	if( !bRet ) {
		DBG_INFO ( "writeReg LastError=%08x\n", GetLastError() );
	}
	return (ret == 1) && bRet;
}

bool EM2874Device::readI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop)
{
	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0xc0;
	spkt.Request = isStop ? 2 : 3;
	spkt.Index = addr;
	spkt.Length = size;

	ULONG ret;
	BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, (PUCHAR)data, spkt.Length, &ret, NULL );
	if( !bRet ) {
		DBG_INFO ( "readI2C LastError=%08x\n", GetLastError() );
		return false;
	}
	readReg( 0x05, (uint8_t*)&ret );
	if ( (uint8_t)ret ) {
		DBG_INFO ( "readI2C(%02X) ProtocolError=%02X\n", addr, (uint8_t)ret );
		return false;
	}
	return true;
}

bool EM2874Device::writeI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop)
{
	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0x40;
	spkt.Request = isStop ? 2 : 3;
	spkt.Index = addr;
	spkt.Length = size;

	ULONG ret;
	BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, (PUCHAR)data, spkt.Length, &ret, NULL );
	if( !bRet ) {
		DBG_INFO ( "writeI2C LastError=%08x\n", GetLastError() );
		return false;
	}
	readReg( 0x05, (uint8_t*)&ret );
	if ( (uint8_t)ret ) {
		DBG_INFO ( "writeI2C(%02X) ProtocolError=%02X\n", addr, (uint8_t)ret );
		return false;
	}
	return true;
}

#ifdef EM2874_ICC
bool EM2874Device::resetICC_1 ()
{
	if(getCardStatus() == 0x5 &&
	writeReg( EM2874_REG_CAS_MODE1, 0x1 ) &&
	writeReg( EM2874_REG_CAS_RESET, 0x1 )
	) {
		return true;
	}
	return false;
}
bool EM2874Device::resetICC_2 ()
{
	uint8_t rbuff[32];
	size_t rlen = sizeof(rbuff);

	DBG_INFO ( "ICC ready\n" );
	readICC( &rlen, rbuff );
	miliWait(1);

	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0x40;
	spkt.Request = 0x14;
	spkt.Index = 0x200;

	static UCHAR cmd[] = { 0x00, 0xc1, 0x01, 0xfe, 0x3e };
	ULONG ret;
	spkt.Length = sizeof(cmd);
	BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, cmd, sizeof(cmd), &ret, NULL );
	if( !bRet ) {
		DBG_INFO ( "writeICC LastError=%08x\n", GetLastError() );
		return false;
	}

	writeReg( EM2874_REG_CAS_MODE2, 0);
	writeReg( EM2874_REG_CAS_STATUS, 0x80 );

	miliWait(100);
	writeReg( EM2874_REG_CAS_DATALEN, 5 );
	if(waitICC() < 0) return false;

	spkt.RequestType = 0xc0;
	spkt.Index = 0;
	bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, rbuff, 4, &ret, NULL );
	if( !bRet || rbuff[1] != 0xe1 || rbuff[3] != 0xfe )
		return false;
	cardPCB = 0;
	isCardReady = true;
	return true;
}

bool EM2874Device::resetICC ()
{
	if(!resetICC_1())
		return false;

	if(waitICC() < 0) {
		writeReg(EM2874_REG_CAS_MODE1, 0x0);
		return false;
	}
	return resetICC_2();
}

bool EM2874Device::writeICC ( const size_t size, const void *data )
{
	uint8_t val;
	if( getCardStatus() != 0x5 )
		return false;
	writeReg( EM2874_REG_CAS_MODE1, 0x01 );

	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0x40;
	spkt.Request = 0x14;
	spkt.Index = 0x200;

	ULONG ret;
	uint8_t cardBuf[256];
	cardBuf[0] = 0;
	cardBuf[1] = cardPCB;
	cardBuf[2] = size;
	::CopyMemory( cardBuf+3, data, size );
	cardBuf[size+3] = ICC_checkSum( cardBuf+1, size+2 );
	val = size + 4;
	for(int i = 0; i < val; i+=64 ) {
		spkt.Index = 0x200 + i;
		spkt.Length = (val - i) > 64 ? 64 : (val - i);

		BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, (PUCHAR)(cardBuf+i)
			, spkt.Length, &ret, NULL );
		if( !bRet ) {
			DBG_INFO ( "writeICC LastError=%08x\n", GetLastError() );
			return false;
		}
	}

	cardPCB ^= 0x40;
	writeReg( EM2874_REG_CAS_MODE2, 0);
	readReg( EM2874_REG_CAS_STATUS, &val );
	//DBG_INFO("Wr r70 %02X\n",val);
	writeReg( EM2874_REG_CAS_DATALEN, size + 4 );
	miliWait(1);
	return true;
}

bool EM2874Device::readICC ( size_t *size, void *data )
{
	uint8_t val;
	val = readReg( EM2874_REG_CAS_DATALEN );
	if( val > *size + 4 || val < 5 )
		return false;
	*size = val - 4;

	WINUSB_SETUP_PACKET spkt;
	::ZeroMemory ( &spkt, sizeof(spkt) );
	spkt.RequestType = 0xc0;
	spkt.Request = 0x14;

	ULONG ret;
	uint8_t cardBuf[256];
	for(int i = 0; i < val; i+=64 ) {
		spkt.Index = i;
		spkt.Length = (val - i) > 64 ? 64 : (val - i);

		BOOL bRet = ::WinUsb_ControlTransfer ( usbHandle, spkt, (PUCHAR)(cardBuf+i)
			, spkt.Length, &ret, NULL );
		if( !bRet ) {
			DBG_INFO ( "readICC LastError=%08x\n", GetLastError() );
			return false;
		}
	}
	memcpy( data, cardBuf+3, val-4 );
	return true;
}

int EM2874Device::waitICC ()
{
	uint8_t val;
	int i;
	for(i = 0; i < 40; i++) {
		miliWait(8);
		if(!readReg(EM2874_REG_CAS_STATUS, &val))
			continue;
		if( val == 5 ) {
			return i;
		}
		if( val == 0 ) return -1;	// card error
	}
	return -2;	// timeout
}

int EM2874Device::getCardStatus()
{
	uint8_t val;
	if(readReg(EM2874_REG_CAS_STATUS, &val)) {
		return val;
	}
	return -1;
}
#endif

int EM2874Device::getDeviceID()
{
	uint8_t buf[2], tuner_reg;
	unsigned val;
	// ROMで判断
	writeReg(EM28XX_REG_I2C_CLK, 0x42);
	buf[0] = 0; buf[1] = 0x6a;	writeI2C(EEPROM_ADDR, 2, buf, false);
	if(!readI2C (EEPROM_ADDR, 2, buf, true))
		return -1;
	val = *(uint16_t*)buf;
	DBG_INFO ("USBPID=%04X ",val);
	if(val == 0x003b || val == 0x0238)
		return 2;

	// Tuner Regで判断
	writeReg(EM28XX_REG_I2C_CLK, 0x44);
	val = 0x00fe;	writeI2C(DEMOD_ADDR, 2, (uint8_t*)&val, true);
	tuner_reg = 0x0;
	writeI2C(TUNER_ADDR, 1, &tuner_reg, false);
	readI2C (TUNER_ADDR, 1, &tuner_reg, true);
	DBG_INFO ("Tuner=%02X ",tuner_reg);
	val = 0x01fe;	writeI2C(DEMOD_ADDR, 2, (uint8_t*)&val, true);

	if(tuner_reg == 0x84)	// TDA18271HD
		return 1;

	return 2;
}

#ifdef EM2874_TS

void EM2874Device::SetBuffer(void *pBuf)
{
	TsBuffSize = (int32_t*)pBuf;
	if(pBuf) {
		TsBuff = (uint8_t*)(TsBuffSize + 0x400);

		TsBuffSize[RINGBUFF_SIZE] = -2;
		TsBuffSize[0x3ff] = -USBBULK_XFERSIZE;
	}
}

bool EM2874Device::TransferStart()
{
	if(TsBuffSize == NULL)	return false;
	if(hTsEvent)	return true;

	if( readReg(0x0B) & 0x2 ) {
		// Isochronous転送設定なら もう阿寒湖 (ここで止めないと、いろいろ死ぬ。)
		return false;
	}

	ULONG bytesRead = 0x1;
	::WinUsb_ResetPipe ( usbHandle, EM2874_EP_TS1 );
	::WinUsb_SetPipePolicy ( usbHandle, EM2874_EP_TS1, RAW_IO, sizeof(UCHAR), &bytesRead );
	::WinUsb_SetPipePolicy ( usbHandle, EM2874_EP_TS1, AUTO_CLEAR_STALL, sizeof(UCHAR), &bytesRead );
	TransferResume();
	//
	TsBuffIndex = 0;

	hTsEvent = ::CreateEvent ( NULL, FALSE, TRUE, NULL );
	for( OverlappedIoIndex = 0; OverlappedIoIndex < NUM_IOHANDLE; OverlappedIoIndex++ ) {
		BeginAsyncRead();
	}
	OverlappedIoIndex = 0;
	dwtLastRead = ::GetTickCount();
	return true;
}

void EM2874Device::TransferStop()
{
	if(hTsEvent == NULL) return;
	TransferPause();

	::CloseHandle(hTsEvent);
	hTsEvent = NULL;
}

void EM2874Device::TransferPause()
{
	writeReg( EM2874_REG_TS_ENABLE, 0 );
	if(hTsEvent == NULL) return;
	::WinUsb_AbortPipe( usbHandle, EM2874_EP_TS1 );
}

void EM2874Device::TransferResume()
{
	writeReg( EM2874_REG_TS_ENABLE, EM2874_TS1_CAPTURE_ENABLE | EM2874_TS1_NULL_DISCARD );
	if(hTsEvent != NULL) {
		::SetEvent(hTsEvent);
	}
}

int EM2874Device::DispatchTSRead()
{
	int nRet;
	int cnt;
	for(cnt = 0; ; cnt++) {
		nRet = GetOverlappedResult();
		if(nRet == -1) {
			// TS転送 待ち
			break;
		}else if(nRet >= 0) {
			// TS転送完了 次の転送を要求
			BeginAsyncRead();
			if(OverlappedIoIndex < (NUM_IOHANDLE-1)) OverlappedIoIndex++;
			else OverlappedIoIndex = 0;
		}else{
			// TS転送 終了
			return -1;
		}
	}
	//if(cnt != 1) DBG_INFO("Disp:%u ", cnt);
	return cnt;
}

int EM2874Device::BeginAsyncRead()
{
	DWORD dRet = TsBuffIndex;
	TsBuffIndex = (dRet < (RINGBUFF_SIZE-1)) ? dRet + 1 : 0;

	TsBuffSize[dRet] = -1;
	IoContext[OverlappedIoIndex].index = dRet;

	::ZeroMemory(&IoContext[OverlappedIoIndex].ol, sizeof(OVERLAPPED));
	IoContext[OverlappedIoIndex].ol.hEvent = hTsEvent;

	BOOL bRet = ::WinUsb_ReadPipe ( usbHandle, EM2874_EP_TS1, TsBuff + dRet*USBBULK_XFERSIZE, USBBULK_XFERSIZE, NULL, &IoContext[OverlappedIoIndex].ol );
#if 0
	dRet = ::GetLastError();
	if( bRet == FALSE && dRet != ERROR_IO_PENDING ) DBG_INFO ("ReadP=%u ",dRet);
	if( bRet ) {
		DBG_INFO ("ReadPs=%u ",dRet);
	}
#endif
	if(bRet) {
		// NOWAITで完了
		::SetEvent(hTsEvent);
	}
	return 0;
}

int EM2874Device::GetOverlappedResult()
{
	if(hTsEvent == NULL) return -2;
	ULONG bytesRead = 0;
	if(FALSE == ::WinUsb_GetOverlappedResult ( usbHandle, &IoContext[OverlappedIoIndex].ol, &bytesRead, FALSE )) {
		DWORD dwRet = ::GetLastError();
		switch(dwRet) {
			case ERROR_SEM_TIMEOUT:
			case ERROR_OPERATION_ABORTED:
				DBG_INFO ("RdAbort%u ",bytesRead);
				bytesRead = 0;
				break;
			case ERROR_IO_INCOMPLETE:
				return -1;
			default:
				DBG_INFO ("OverRes=%u ",dwRet);
				break;
		}
	}
	if(TsBuffSize == NULL) return -2;
	const unsigned idx = IoContext[OverlappedIoIndex].index;
	TsBuffSize[idx] = bytesRead;
	return bytesRead;
}

HANDLE EM2874Device::GetHandle()
{ return hTsEvent; }

#endif
