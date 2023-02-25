/* fsusb2i   (c) 2015-2016 trinity19683
  BonTuner.DLL (MS-Windows)
  BonTuner.h
  2016-02-02
*/
#pragma once

#include "../inc/IBonDriver2.h"
extern "C" {
#include "../it9175.h"
#include "../tsthread.h"
#include "../tsbuff.h"
}

#include "../bonhybrid.h"

namespace FSUSB2i {

class CBonTuner : public CBonFSHybrid
{
protected:
	const TCHAR *RegName() ;
	int UserDecidedDeviceIdx() ;
	void LoadValues(const IValueLoader *Loader) ;
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

	HANDLE m_hDev;
	HANDLE m_hUsbDev;
	struct usb_endpoint_st  m_USBEP;
	it9175_state pDev;

};

} // End of namespace FSUSB2i

