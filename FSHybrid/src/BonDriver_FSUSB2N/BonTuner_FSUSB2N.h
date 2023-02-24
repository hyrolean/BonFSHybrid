#pragma once

// BonTuner.h: CBonTuner クラスのインターフェイス
//////////////////////////////////////////////////

#include "IBonDriver2.h"
#include "../ktv.h"

class CBonTuner : public IBonDriver2
{
public:
	CBonTuner();
	virtual ~CBonTuner();

// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);

	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);

	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);

	void PurgeTsStream(void);

// IBonDriver2(暫定)
	LPCTSTR GetTunerName(void);

	const BOOL IsTunerOpening(void);
	
	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);

	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);
	
	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

	void Release(void);

	static CBonTuner * m_pThis;
	static HINSTANCE m_hModule;

protected:
	DWORD m_dwCurSpace;
	DWORD m_dwCurChannel;
	DWORD *m_ChannelList;

	EM2874Device *usbDev;
	KtvDevice *pDev;
	BYTE *m_pTsBuff;
	volatile int *m_TsBuffSize;		// 32bit signed
	volatile int m_indexTsBuff;

#ifndef NO_TSTHREAD
	HANDLE m_hThread;
	HANDLE m_hTsRecv;
	static unsigned int __stdcall TsThread (PVOID pv);
#endif

	bool LoadData ();
	void ReadRegMode (HKEY hPKey);
	void ReadRegChannels (HKEY hPKey);
};
