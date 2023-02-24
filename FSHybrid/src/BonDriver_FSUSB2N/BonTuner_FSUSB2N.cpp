
#include "stdafx.h"
#include "BonTuner.h"
#include "../twindbg.h"

#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
	return (CBonTuner::m_pThis)? CBonTuner::m_pThis : ((IBonDriver *) new CBonTuner);
}
#pragma warning( default : 4273 )

// 静的メンバ初期化
CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;

const TCHAR *g_RegKey = TEXT("Software\\tri.dw.land.to\\FSUSB2N");

CBonTuner::CBonTuner()
: m_dwCurSpace(0) , m_dwCurChannel(0) , usbDev(NULL) , pDev(NULL) , m_TsBuffSize(NULL) , m_ChannelList(NULL)
#ifndef NO_TSTHREAD
, m_hThread(NULL)
#endif
{
	m_pThis = this;
	this->LoadData();
}

CBonTuner::~CBonTuner()
{
	// 開かれてる場合は閉じる
	CloseTuner();

	if(m_ChannelList != NULL) ::GlobalFree(m_ChannelList);
	m_pThis = NULL;
}

inline void WaitSleep (const DWORD dwStart, const DWORD dwTime)
{
	DWORD dwLeft = dwStart + dwTime - ::GetTickCount();
	if(dwLeft < 60000U)
		::Sleep(dwLeft);
}

const BOOL CBonTuner::OpenTuner()
{
	CloseTuner();

	if(IsTunerOpening())return FALSE;

	try{
		// AllocTuner
		for(int idx = 0;;)
		{
			EM2874Device* pDevTmp = EM2874Device::AllocDevice(idx);
			if(pDevTmp == NULL) {
				// Deviceが見つからない場合
				throw (const DWORD)__LINE__;
			}
			// Deviceを確保した
			usbDev = pDevTmp;
			break;
		}
		// Device初期化
		usbDev->initDevice2();

		DWORD dwTime = ::GetTickCount();
		::Sleep(80);
		if(usbDev->getDeviceID() == 2) {
			pDev = new Ktv2Device(usbDev);
		}else{
			pDev = new Ktv1Device(usbDev);
		}

		WaitSleep(dwTime, 160);
		pDev->InitTuner();

		WaitSleep(dwTime, 180);
		pDev->InitDeMod();
		pDev->ResetDeMod();

		WaitSleep(dwTime, 500);
		//
		m_TsBuffSize = (int*)VirtualAlloc( NULL, RINGBUFF_SIZE*USBBULK_XFERSIZE + 0x1000, MEM_COMMIT, PAGE_READWRITE );
		if(m_TsBuffSize == NULL)	throw (const DWORD)__LINE__;
		m_pTsBuff = (BYTE*)(m_TsBuffSize + 0x400);
		m_indexTsBuff = 0;

		usbDev->SetBuffer( (void*)m_TsBuffSize );
		usbDev->TransferStart();
#ifndef NO_TSTHREAD
		m_hThread = (HANDLE)_beginthreadex( NULL, 0, TsThread, (PVOID)this, 0, NULL );
		if(m_hThread == INVALID_HANDLE_VALUE) {
			m_hThread = NULL;
		}else{
			::SetThreadPriority( m_hThread, THREAD_PRIORITY_TIME_CRITICAL );
			m_hTsRecv = ::CreateEvent ( NULL, FALSE, FALSE, NULL );
		}
#endif
		// デバイス使用準備完了 選局はまだ
	}
	catch (const DWORD dwErrorStep) {
		// Error
		DBG_INFO("BonDriver_FSUSB2N:OpenTuner dwErrorStep = %lu\n", dwErrorStep);

		CloseTuner();
		return FALSE;
	}
	return TRUE;
}

void CBonTuner::CloseTuner()
{
	if(m_TsBuffSize)
	{
		usbDev->TransferStop();
		usbDev->SetBuffer(NULL);
		::VirtualFree( (LPVOID)m_TsBuffSize, 0, MEM_RELEASE );
		m_TsBuffSize = NULL;
	}

#ifndef NO_TSTHREAD
	if(m_hThread != NULL) {
		if(::WaitForSingleObject(m_hThread, 1500) != WAIT_OBJECT_0) {
			::TerminateThread(m_hThread, 0);
		}
		::CloseHandle(m_hThread);
		::CloseHandle(m_hTsRecv);
		m_hThread = NULL;
	}
#endif

	if(pDev) {
		delete pDev;
		pDev = NULL;
	}

	if(usbDev) {
		delete usbDev;
		usbDev = NULL;
	}
}

const BOOL CBonTuner::SetChannel(const BYTE bCh)
{
	// IBonDriverとの互換性を保つために暫定

	if(bCh < 13 || bCh > 52) return FALSE;
	else return SetChannel(0UL, bCh - 13U);
}

const float CBonTuner::GetSignalLevel(void)
{
	if(pDev == NULL) return 0.0f;
	return pDev->DeMod_GetQuality() * 0.01f;
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	if(GetReadyCount() > 0)
	{
		return WAIT_OBJECT_0;
	}
	if(m_TsBuffSize == NULL)
	{
		::Sleep(dwTimeOut < 2000 ? dwTimeOut : 2000 );
		return WAIT_TIMEOUT;
	}
#ifdef NO_TSTHREAD
	DWORD dwRet = ::WaitForSingleObject( usbDev->GetHandle() , dwTimeOut );

	if( dwRet == WAIT_OBJECT_0  )
	{
		int nRet = usbDev->DispatchTSRead();
		if( nRet < 0 )	return WAIT_FAILED;
	}
	return dwRet;
#else
	return ::WaitForSingleObject(m_hTsRecv, dwTimeOut);
#endif
}

const DWORD CBonTuner::GetReadyCount()
{
	// 取り出し可能TSデータ数を取得する
	if(m_TsBuffSize == NULL) return 0;
	const int indexCurrent = m_indexTsBuff;
	int val;

#ifdef NO_TSTHREAD
	DWORD dwRet = ::WaitForSingleObject( usbDev->GetHandle() , 0 );

	if( dwRet == WAIT_FAILED )
	{
		return 0;
	}else if( dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT )
	{
		usbDev->DispatchTSRead();
	}
#endif

	// size=0をskip
	do {
		val = m_TsBuffSize[m_indexTsBuff];
		if(val > 0 || val == -1) {
			break;
		}else if(val <= -2) {
			m_indexTsBuff = 0;
		}else if(val == 0) {
			m_indexTsBuff++;
		}
	} while(m_indexTsBuff != indexCurrent);

	return m_TsBuffSize[m_indexTsBuff] < 0 ? 0 : 1;
}

const BOOL CBonTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;

	// TSデータをBufferから取り出す
	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize) {
			::CopyMemory(pDst, pSrc, *pdwSize);
		}
		return TRUE;
	}
	
	return FALSE;
}

const BOOL CBonTuner::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	//
	if(m_TsBuffSize == NULL) return FALSE;
	const unsigned int BuffBlockSize = -m_TsBuffSize[0x3ff];
	int val;

	if(GetReadyCount() == 0) {
		// 取り出し可能なデータがない
		*pdwSize = 0;
		*pdwRemain = 0;
		return TRUE;
	}

	*ppDst = m_pTsBuff + (m_indexTsBuff * BuffBlockSize);
	int dataLen = 0;
	do {
		val = m_TsBuffSize[m_indexTsBuff];

		if(val >= 0) {
			dataLen += val;
			m_indexTsBuff++;
		}
	} while(val == BuffBlockSize);

	*pdwSize = dataLen;
	*pdwRemain = GetReadyCount();
	return TRUE;
}

void CBonTuner::PurgeTsStream()
{
	if(m_TsBuffSize == NULL) return;
	if(m_TsBuffSize[m_indexTsBuff] < 0) return;

	// Bufferから取り出し可能データをパージする
	for(int i = 0; i <= 0x3fe; i++) {
		if(m_TsBuffSize[i] == -1)
		{
			m_indexTsBuff = i;
			return;
		}else if(m_TsBuffSize[i] == -2)
		{
			return;
		}
	}
}

void CBonTuner::Release()
{
	// インスタンス開放
	delete this;
}

LPCTSTR CBonTuner::GetTunerName(void)
{
	// チューナ名を返す
	return TEXT("KTV-FSUSB2新");
}

const BOOL CBonTuner::IsTunerOpening(void)
{
	return pDev ? TRUE : FALSE;
}

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	// 使用可能なチューニング空間を返す
	if(dwSpace == 0U) return TEXT("地デジ");
	else if(m_ChannelList != NULL && dwSpace == 1U) return TEXT("地デジ(追加)");
	return NULL;
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	// 使用可能なChannelを返す

	static const TCHAR ChannelNameT[][3] = 
	{
		TEXT("13"), TEXT("14"), TEXT("15"), TEXT("16"), TEXT("17"), TEXT("18"), TEXT("19"),
		TEXT("20"), TEXT("21"), TEXT("22"), TEXT("23"), TEXT("24"), TEXT("25"), TEXT("26"), TEXT("27"), TEXT("28"), TEXT("29"),
		TEXT("30"), TEXT("31"), TEXT("32"), TEXT("33"), TEXT("34"), TEXT("35"), TEXT("36"), TEXT("37"), TEXT("38"), TEXT("39"), 
		TEXT("40"), TEXT("41"), TEXT("42"), TEXT("43"), TEXT("44"), TEXT("45"), TEXT("46"), TEXT("47"), TEXT("48"), TEXT("49"), 
		TEXT("50"), TEXT("51"), TEXT("52")
	};

	if(m_ChannelList != NULL && dwSpace == 1U) {
		/* ユーザー定義 */
		DWORD dwChannelLen		= m_ChannelList[0] >> 16;
		DWORD dwNumOfChannels	= m_ChannelList[0] & 0xffff;
		TCHAR *ptrStr = (TCHAR*)(m_ChannelList + dwNumOfChannels + 1);
		if(dwChannel < dwNumOfChannels)	return ptrStr + (dwChannelLen * dwChannel);
	}else if(dwSpace == 0U) {
		if(dwChannel < 40)
			return ChannelNameT[dwChannel];
	}
	return NULL;
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if(pDev == NULL) return FALSE;
	if(NULL == EnumChannelName(dwSpace, dwChannel)) return FALSE;

	DWORD dwFreq;

	if(m_ChannelList != NULL && dwSpace == 1U) {
		/* ユーザー定義 */
		dwFreq = m_ChannelList[dwChannel + 1];
	}else if(dwSpace == 0U) {
		dwFreq = dwChannel * 6000U + 473143U;
	}else{
		return FALSE;
	}
	if(dwFreq < 90000U || dwFreq > 772000U) return FALSE;

	// Channel変更
	usbDev->TransferPause();

	pDev->SetFrequency(dwFreq);
	::Sleep(5);
	pDev->ResetDeMod();
	::Sleep(20);
	usbDev->TransferResume();
	::Sleep(10);
	PurgeTsStream();

	// Channel情報更新
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;

	return TRUE;
}

const DWORD CBonTuner::GetCurSpace(void)
{
	// 現在のチューニング空間を返す
	return m_dwCurSpace;
}

const DWORD CBonTuner::GetCurChannel(void)
{
	// 現在のChannelを返す
	return m_dwCurChannel;
}

#ifndef NO_TSTHREAD
unsigned int __stdcall CBonTuner::TsThread (PVOID pv)
{
	DWORD dwRet;
	int nRet;
	EM2874Device *pUsbDev;

	if(pv && ((CBonTuner*)pv)->usbDev)
	{
		pUsbDev = ((CBonTuner*)pv)->usbDev;
	}else{
		::_endthreadex (0);
		return 0;
	}

	for(;;)
	{
		dwRet = ::WaitForSingleObject( pUsbDev->GetHandle() , 1000 );

		if( dwRet == WAIT_FAILED )
		{
			break;
		}else if( dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT )
		{
			nRet = pUsbDev->DispatchTSRead();
			if(nRet > 0)	::SetEvent(((CBonTuner*)pv)->m_hTsRecv);
			if(nRet < 0)	break;
		}
	}
	::_endthreadex (0);
	return 0;
}
#endif


bool CBonTuner::LoadData ()
{
	HKEY hKey;

	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, g_RegKey, 0, KEY_READ, &hKey))
	{
		ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}
	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, g_RegKey, 0, KEY_READ, &hKey))
	{
		ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}

	return true;
}

void CBonTuner::ReadRegMode (HKEY hPKey)
{
	DWORD dwValue, dwLen, dwType;

	dwLen = sizeof(dwValue);
	if(ERROR_SUCCESS != RegQueryValueEx( hPKey, TEXT("FunctionMode"), NULL, &dwType, (BYTE*)&dwValue, &dwLen)
		|| dwLen != sizeof(DWORD) ) {
		return;
	}
	EM2874Device::UserSettings = dwValue & 0xffff;
	KtvDevice::UserSettings = dwValue >> 16;
}

void CBonTuner::ReadRegChannels (HKEY hPKey)
{
	if(m_ChannelList != NULL) return;
	//
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
	m_ChannelList = (DWORD*) GlobalAlloc(GMEM_FIXED, NumOfValues * (dwMaxValueName * sizeof(TCHAR) + sizeof(DWORD)) + sizeof(DWORD) );
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
		dwByte = dwValue >> 24; // Index
		if( dwByte >= NumOfValues ) continue;
		m_ChannelList[dwByte + 1] = dwValue & 0x00ffffffU;
		ptrStr = (TCHAR*)(m_ChannelList + NumOfValues + 1);
		ptrStr += dwMaxValueName * dwByte;
		lstrcpyn( ptrStr, szValueName, dwMaxValueName );
	}
	RegCloseKey(hKey);
}
