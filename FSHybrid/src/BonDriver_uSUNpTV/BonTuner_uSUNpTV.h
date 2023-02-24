/* SunPTV-USB   (c) 2016 trinity19683
  BonTuner.DLL (MS-Windows)
  BonTuner.h
  2016-01-23
*/
#pragma once

#include "../inc/IBonDriver2.h"
extern "C" {
#include "../em287x.h"
#include "../tsthread.h"
}

class CBonTuner : public IBonDriver2
{
public:
	CBonTuner();
	virtual ~CBonTuner();

//# IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);

	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);

	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);

	void PurgeTsStream(void);

//# IBonDriver2
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

	HANDLE m_hDev;
	HANDLE m_hUsbDev;
	struct usb_endpoint_st  m_USBEP;
	em287x_state pDev;
	void* demodDev;
	void* tunerDev[2];
	int m_selectedTuner;
	tsthread_ptr tsthr;

	bool LoadData ();
	void ReadRegMode (HKEY hPKey);
	void ReadRegChannels (HKEY hPKey);
};
