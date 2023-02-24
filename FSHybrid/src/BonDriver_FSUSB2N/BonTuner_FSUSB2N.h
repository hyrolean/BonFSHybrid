#pragma once

// BonTuner.h: CBonTuner クラスのインターフェイス
//////////////////////////////////////////////////

#include <vector>
#include <queue>
#include <cstdlib>
#include "IBonDriver2.h"
#include "../ktv.h"
#include "../usbdevfile.h"
#include "../bonhybrid.h"

namespace FSUSB2N {

class CBonTuner : public CBonFSHybrid
{
protected:
	const TCHAR *RegName() ;
	int UserDecidedDeviceIdx() ;
    void LoadValues(const IValueLoader *Loader);

public:
	CBonTuner();
	virtual ~CBonTuner();

// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const float GetSignalLevel(void);

// IBonDriver2(暫定)
	LPCTSTR GetTunerName(void);

	const BOOL IsTunerOpening(void);

	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);

	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

	void Release(void);

protected:
	DWORD m_dwCurSpace;
	DWORD m_dwCurChannel;

	HANDLE m_hDev;
	HANDLE m_hUsbDev;
	struct usb_endpoint_st  m_USBEP;
	EM2874Device *usbDev;
	KtvDevice *pDev;

};

} // End of namespace FSUSB2N

