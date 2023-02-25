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
#include "../tsbuff.h"
}

#include "../bonhybrid.h"

namespace uSUNpTV {

class CBonTuner : public CBonFSHybrid
{
protected:
	const TCHAR *RegName() ;
	int UserDecidedDeviceIdx() ;
	virtual void LoadValues(const IValueLoader *Loader);
	const BOOL TryOpenTuner(void);

public:
	CBonTuner();
	virtual ~CBonTuner();

//# IBonDriver
	void CloseTuner(void);

	const float GetSignalLevel(void);

//# IBonDriver2
	LPCTSTR GetTunerName(void);

	const BOOL IsTunerOpening(void);

	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);

	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

	void Release(void);

protected:
	DWORD m_dwCurSpace;
	DWORD m_dwCurChannel;
    BOOL m_hasStream;
	CHANNEL m_chCur ;

	HANDLE m_hDev;
	HANDLE m_hUsbDev;
	struct usb_endpoint_st  m_USBEP;
	em287x_state pDev;
	void* demodDev;
	void* tunerDev[2];
	int m_selectedTuner;

};

} // End of namespace uSUNpTV

