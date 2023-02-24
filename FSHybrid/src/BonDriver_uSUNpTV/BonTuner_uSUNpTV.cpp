/* SunPTV-USB   (c) 2016 trinity19683
  BonTuner.DLL (MS-Windows)
  BonTuner.cpp
  2016-02-10
*/

#include "stdafx.h"
#include <tchar.h>
#include "BonTuner.h"
#include "../usbdevfile.h"
extern "C" {
#include "../tc90522.h"
#include "../tda20142.h"
#include "../mxl136.h"
}

#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{ return (CBonTuner::m_pThis)? CBonTuner::m_pThis : ((IBonDriver *) new CBonTuner); }
#pragma warning( default : 4273 )

//# initialize static member variables
CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;

const TCHAR* const g_RegKey = TEXT("Software\\trinity19683\\FSUSB2i");

CBonTuner::CBonTuner()
: m_dwCurSpace(123), m_dwCurChannel(0), m_hDev(NULL), m_hUsbDev(NULL), pDev(NULL), demodDev(NULL), m_selectedTuner(-1), tsthr(NULL),
 m_ChannelList(NULL)
{
	int i;
	m_pThis = this;
	for(i = 0; i < 2; i++ ) {
		tunerDev[i] = NULL;
	}
}

CBonTuner::~CBonTuner()
{
	CloseTuner();

	if(m_ChannelList != NULL) ::GlobalFree(m_ChannelList);
	m_pThis = NULL;
}

const BOOL CBonTuner::OpenTuner()
{
	//# if already open, close tuner
	CloseTuner();
	if(IsTunerOpening()) return FALSE;

	try{
		//# AllocTuner
		for(unsigned int idx = 0; idx < 40;) {
			HANDLE hDev;
			if((hDev = usbdevfile_alloc(&idx) ) == NULL) {   //# not found
				throw (const DWORD)__LINE__;
			}
			//# found
			m_hDev = hDev;
			if((hDev = usbdevfile_init(m_hDev) ) == NULL) {   //# failed
				throw (const DWORD)__LINE__;
			}
			m_hUsbDev = hDev;
			break;
		}
		//# device initialize
		m_USBEP.fd = m_hUsbDev;
		if( em287x_create(&pDev, &m_USBEP) ) {
			throw (const DWORD)__LINE__;
		}
		struct i2c_device_st* pI2C;
		//# demod
		if( tc90522_create(&demodDev) ) {
			throw (const DWORD)__LINE__;
		}
		pI2C = (struct i2c_device_st*)tc90522_i2c_ptr(demodDev);
		pI2C->addr = 0x20;
		em287x_attach(pDev, pI2C);
		if( tc90522_init(demodDev) ) {
			throw (const DWORD)__LINE__;
		}
		//# tuner 0 terra
		if( mxl136_create(&tunerDev[0]) ) {
			throw (const DWORD)__LINE__;
		}
		pI2C = (struct i2c_device_st*)mxl136_i2c_ptr(tunerDev[0]);
		pI2C->addr = 0xc0;
		tc90522_attach(demodDev, 0, pI2C);
		if( mxl136_init(tunerDev[0]) ) {
			throw (const DWORD)__LINE__;
		}
		//# tuner 1 BS/CS
		if( tda20142_create(&tunerDev[1]) ) {
			throw (const DWORD)__LINE__;
		}
		pI2C = (struct i2c_device_st*)tda20142_i2c_ptr(tunerDev[1]);
		pI2C->addr = 0xa8;
		tc90522_attach(demodDev, 1, pI2C);
		if( tda20142_init(tunerDev[1]) ) {
			throw (const DWORD)__LINE__;
		}
		//# demod set params
		if( tc90522_selectDevice(demodDev, 1) ) {
			throw (const DWORD)__LINE__;
		}
		//# TS receive thread
		if( tsthread_create(&tsthr, &m_USBEP) ) {
			throw (const DWORD)__LINE__;
		}

		//# device has been ready.
		LoadData();
	}
	catch (const DWORD dwErrorStep) {
		//# Error
		warn_msg(0,"BonDriver_uSUNpTV:OpenTuner dwErrorStep = %u", dwErrorStep);

		CloseTuner();
		return FALSE;
	}
	return TRUE;
}

void CBonTuner::CloseTuner()
{
	if(tsthr) {
		tsthread_stop(tsthr);
		tsthread_destroy(tsthr);
		tsthr = NULL;
	}
	if(tunerDev[0]) {
		mxl136_destroy(tunerDev[0]);
		tunerDev[0] = NULL;
	}
	if(tunerDev[1]) {
		tda20142_destroy(tunerDev[1]);
		tunerDev[1] = NULL;
	}
	if(demodDev) {
		tc90522_destroy(demodDev);
		demodDev = NULL;
	}
	if(pDev) {
		em287x_destroy(pDev);
		pDev = NULL;
	}
	if(m_hUsbDev) {
		usbdevfile_free(m_hUsbDev);
		m_hUsbDev = NULL;
	}
	if(m_hDev) {
		::CloseHandle( m_hDev );
		m_hDev = NULL;
	}
}

const BOOL CBonTuner::SetChannel(const BYTE bCh)
{
	//# compatible with IBonDriver
	if(bCh < 13 || bCh > 52) return FALSE;
	else return SetChannel(0, bCh - 13);
}

const float CBonTuner::GetSignalLevel(void)
{
	if(0 > m_selectedTuner || (! demodDev) ) return -3.1f;
	unsigned statData[4];
	if(tc90522_readStatistic(demodDev, m_selectedTuner, statData) ) return -3.2f;
	return statData[1] * 0.01f;
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	const int remainTime = (dwTimeOut < 0x10000000) ? dwTimeOut : 0x10000000;
	if(! tsthr) return WAIT_FAILED;

	const int r = tsthread_wait(tsthr, remainTime);
	if(0 > r)  return WAIT_FAILED;
	else if(0 < r)  return WAIT_OBJECT_0;
	else  return WAIT_TIMEOUT;
}

const DWORD CBonTuner::GetReadyCount()
{//# number of call GetTsStream()
	if(! tsthr) return 0;
	const int ret = tsthread_readable(tsthr);
	return (ret > 0) ? 1 : 0;
}

const BOOL CBonTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;
	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize) ::CopyMemory(pDst, pSrc, *pdwSize);
		return TRUE;
	}
	return FALSE;
}

const BOOL CBonTuner::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if(! tsthr) return FALSE;
	const int ret = tsthread_readable(tsthr);
	if(ret <= 0) {
		//# no readable data
		*pdwSize = 0;
		*pdwRemain = 0;
		return TRUE;
	}
	*pdwSize = tsthread_read(tsthr, (void**)ppDst);
	*pdwRemain = GetReadyCount();
	//dmsg("GetTsStream(%p,%u,%u)", ppDst, *pdwSize, *pdwRemain);
	return TRUE;
}

void CBonTuner::PurgeTsStream()
{
	if(! tsthr) return;
	//# purge available data in TS buffer
	tsthread_read(tsthr, NULL);
}

void CBonTuner::Release()  //# release the instance
{ delete this; }

LPCTSTR CBonTuner::GetTunerName(void)
{ return TEXT("‚³‚ñ‚ÏU"); }

const BOOL CBonTuner::IsTunerOpening(void)
{ return m_hUsbDev ? TRUE : FALSE; }

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	if(0 == dwSpace)  return TEXT("’nƒfƒW");
	else if(1 == dwSpace)  return TEXT("BS");
	else if(2 == dwSpace)  return TEXT("CS");
	return NULL;
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	static TCHAR buf[6];
	if(0 == dwSpace) {
		if(m_ChannelList != NULL) {
			//# User-defined channels
			const DWORD dwChannelLen    = m_ChannelList[0] >> 16;
			const DWORD dwNumOfChannels = m_ChannelList[0] & 0xFFFF;
			TCHAR* const ptrStr = (TCHAR*)(m_ChannelList + dwNumOfChannels + 1);
			if(dwChannel < dwNumOfChannels)	return ptrStr + (dwChannelLen * dwChannel);
		}else if(dwChannel < 40) {
			_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%u"), dwChannel + 13);
			return buf;    //# The caller must copy data from this buffer.
		}
	}else if(1 == dwSpace && dwChannel < 12 * 8) {
		_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%02u.%u"), (dwChannel >> 3)*2 + 1, dwChannel & 0x7);
		return buf;
	}else if(2 == dwSpace && dwChannel < 12 * 8) {
		_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%02u.%u"), (dwChannel >> 3)*2 + 2, dwChannel & 0x7);
		return buf;
	}
	return NULL;
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	DWORD dwFreq = 0;
	int tunerNum = 0;
	bool hasStream = TRUE;

	if(0 == dwSpace) {
		if(m_ChannelList != NULL) {  //# User-defined channels
			const DWORD dwNumOfChannels = m_ChannelList[0] & 0xFFFF;
			if(dwChannel < dwNumOfChannels)
				dwFreq = m_ChannelList[dwChannel + 1];
		}else{  //# UHF standard channels
			if(dwChannel < 40)
				dwFreq = dwChannel * 6000 + 473143;
		}
	}else if(1 == dwSpace && dwChannel < 12 * 8) {
		dwFreq = (dwChannel >> 3) * 38360 + 1049480;
	}else if(2 == dwSpace && dwChannel < 12 * 8) {
		dwFreq = (dwChannel >> 3) * 40000 + 1613000;
	}else if(dwSpace == 114514) {  //# dwChannel as freq/kHz
		dwFreq = dwChannel;
	}
	if(dwFreq < 60000 || dwFreq > 2456123 ) {
		warn_msg(0,"BonDriver_uSUNpTV:SetChannel(%u,%u) invalid!", dwSpace, dwChannel);
		return FALSE;
	}
	if( dwFreq >= 900000 ) tunerNum = 1;

	//# change channel
	if(tsthr) tsthread_stop(tsthr);

	if(tunerNum != m_selectedTuner) {
		if( tc90522_selectDevice(demodDev, tunerNum) ) return FALSE;
		if(tunerNum & 0x1) {
			mxl136_sleep(tunerDev[0]);
		}else{
			mxl136_wakeup(tunerDev[0]);
		}
	}
	if(tunerNum & 0x1) {
		if( m_selectedTuner != tunerNum || (m_dwCurChannel ^ dwChannel) >> 3 ) {
			if( tda20142_setFreq(tunerDev[1], dwFreq) ) return FALSE;
			::Sleep( 30 );
			if( tc90522_resetDemod(demodDev, tunerNum ) ) return FALSE;
			::Sleep( 50 );
		}
		DWORD dwTime = ::GetTickCount() + 800;
		hasStream = FALSE;
		do {
			int ret = tc90522_selectStream(demodDev, tunerNum, dwChannel & 0x7 );
			if(0 == ret) {
				hasStream = TRUE;
				break;
			}else if(0 > ret)  return FALSE;
			::Sleep( 40 );
		} while( (int)(dwTime - ::GetTickCount()) > 0);
	}else{
		if( mxl136_setFreq(tunerDev[0], dwFreq) ) return FALSE;
		::Sleep( 30 );
		if( tc90522_resetDemod(demodDev, tunerNum ) ) return FALSE;
		::Sleep( 50 );
	}
	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	m_selectedTuner = tunerNum;

	if(tsthr && hasStream) tsthread_start(tsthr);

	DWORD dwTime = ::GetTickCount() + 480;
	do {
		unsigned statData[4];
		::Sleep( 40 );
		if( tc90522_readStatistic(demodDev, tunerNum, statData) ) continue;
		if( statData[0] & 0x10 ) break;
	} while( (int)(dwTime - ::GetTickCount()) > 0);

	PurgeTsStream();

	return TRUE;
}

const DWORD CBonTuner::GetCurSpace(void)
{ return m_dwCurSpace; }

const DWORD CBonTuner::GetCurChannel(void)
{ return m_dwCurChannel; }


bool CBonTuner::LoadData ()
{
	HKEY hKey;

	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, g_RegKey, 0, KEY_READ, &hKey)) {
		//ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}
	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, g_RegKey, 0, KEY_READ, &hKey)) {
		//ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}

	return true;
}

void CBonTuner::ReadRegMode (HKEY hPKey)
{ return; }

void CBonTuner::ReadRegChannels (HKEY hPKey)
{
	if(m_ChannelList != NULL) return;

	HKEY hKey;
	DWORD NumOfValues;
	TCHAR szValueName[32];
	DWORD dwValue, dwLen, dwType, dwByte, dwMaxValueName;
	if(ERROR_SUCCESS != RegOpenKeyEx( hPKey, TEXT("Channels"), 0, KEY_READ, &hKey)) {
		return;
	}
	if(ERROR_SUCCESS != RegQueryInfoKey( hKey, NULL, NULL, NULL, NULL, NULL, NULL, &NumOfValues, &dwMaxValueName, NULL, NULL, NULL)) {
		RegCloseKey(hKey);
		return;
	}
	dwMaxValueName++;
	m_ChannelList = (DWORD*) ::GlobalAlloc(GMEM_FIXED, NumOfValues * (dwMaxValueName * sizeof(TCHAR) + sizeof(DWORD)) + sizeof(DWORD) );
	m_ChannelList[0] = dwMaxValueName << 16 | NumOfValues;
	ZeroMemory( m_ChannelList + 1, sizeof(DWORD) * NumOfValues );
	TCHAR *ptrStr;
	for(DWORD dwIdx = 0; dwIdx < NumOfValues; dwIdx++ ) {
		dwLen = 32;
		dwByte = sizeof(dwValue);
		if(ERROR_SUCCESS != RegEnumValue( hKey, dwIdx, szValueName, &dwLen, NULL, &dwType, (BYTE*)&dwValue, &dwByte)
			|| dwByte != sizeof(DWORD)) {
			break;
		}
		dwByte = dwValue >> 24; //# Index
		if( dwByte >= NumOfValues ) continue;
		m_ChannelList[dwByte + 1] = dwValue & 0x00ffffff;
		ptrStr = (TCHAR*)(m_ChannelList + NumOfValues + 1);
		ptrStr += dwMaxValueName * dwByte;
		lstrcpyn( ptrStr, szValueName, dwMaxValueName );
	}
	RegCloseKey(hKey);
}

/*EOF*/