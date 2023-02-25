/* fsusb2i   (c) 2015-2016 trinity19683
  BonTuner.DLL (MS-Windows)
  BonTuner.cpp
  2016-02-12
*/
#include "stdafx.h"

#include <tchar.h>
#include "BonTuner_FSUSB2i.h"
#include "../usbdevfile.h"

using namespace std ;

namespace FSUSB2i {

DWORD FSUSB2I_SETFREQ_TIMES     = 2 ;
DWORD FSUSB2I_TUNING_WAIT       = 1500 ;
BOOL  FSUSB2I_LOCK_ON_SIGNAL    = TRUE ;

const TCHAR* const g_RegKey = TEXT("Software\\trinity19683\\FSUSB2i");

CBonTuner::CBonTuner()
: CBonFSHybrid(), m_dwCurSpace(0), m_dwCurChannel(0), m_hDev(NULL), m_hUsbDev(NULL), pDev(NULL)
{}

CBonTuner::~CBonTuner()
{
	CloseTuner();
}

const TCHAR *CBonTuner::RegName()
{
	return g_RegKey ;
}

int CBonTuner::UserDecidedDeviceIdx()
{
	int idx=0 ;
	if(sscanf_s( upper_case(file_prefix_of(ModuleFileName())).c_str() , "BONDRIVER_FSUSB2I_DEV%d", &idx )==1)
	  return idx ;

	return -1 ;
}

const BOOL CBonTuner::OpenTuner()
{
	//# if already open, close tuner
	CloseTuner();
	if(IsTunerOpening()) return FALSE;

	try{
		//# AllocTuner
		if(!FindDevice(GUID_WINUSB_FSUSB2V3_DRV,m_hDev,m_hUsbDev)) {
		  if(m_hDev==NULL) throw (const DWORD)__LINE__;
		  if(m_hUsbDev==NULL) throw (const DWORD)__LINE__;
		}
		//# device initialize
		m_USBEP.fd = m_hUsbDev;
		if(it9175_create(&pDev, &m_USBEP) != 0) throw (const DWORD)__LINE__;
		//# fifo
		if(!FifoInitialize(&m_USBEP)) throw (const DWORD)__LINE__;

		//# device has been ready.
		//LoadData();
	}
	catch (const DWORD dwErrorStep) {
		//# Error
		warn_msg(0,"BonDriver_FSUSB2i:OpenTuner dwErrorStep = %u", dwErrorStep);

		CloseTuner();
		return FALSE;
	}
	return TRUE;
}

void CBonTuner::CloseTuner()
{
	FifoFinalize() ;
	if(pDev) {
		it9175_destroy(pDev);
		pDev = NULL;
	}
	FreeDevice(m_hDev,m_hUsbDev);
	m_USBEP.dev=NULL;
}

const float CBonTuner::GetSignalLevel(void)
{
	if(NULL == pDev) return 0.0f;
    
    bool do_locking = FSUSB2I_LOCK_ON_SIGNAL && m_USBEP.dev && m_USBEP.lockunlockFunc ;
    
    if(do_locking) //# lock
		m_USBEP.lockunlockFunc(m_USBEP.dev,1);

	uint8_t dB; float lv ;
	if(it9175_readSNRatio(pDev, &dB) != 0) lv = 0.1f;
	else lv = dB * 1.0f;

    if(do_locking) //# unlock
		m_USBEP.lockunlockFunc(m_USBEP.dev,0);
    return lv ;
}

void CBonTuner::Release()  //# release the instance
{ delete this; }

LPCTSTR CBonTuner::GetTunerName(void)
{ return TEXT("FSUSB2i"); }

const BOOL CBonTuner::IsTunerOpening(void)
{ return m_hUsbDev ? TRUE : FALSE; }

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if(NULL == pDev) return FALSE;
	if(NULL == tsthr) return FALSE;

	DWORD dwFreq = GetChannel(dwSpace,dwChannel).Freq ;

	if(dwFreq < 61000 || dwFreq > 874000 ) return FALSE;

	//# change channel
	FifoStop();

	int ret;
	int cnt=0 ;
	do {
	  if(it9175_setFreq(pDev, dwFreq) != 0) {
		if(++cnt==FSUSB2I_SETFREQ_TIMES) return FALSE;
		continue ;
	  }
	  break ;
	}while(1) ;
	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	::Sleep( 80 );

	FifoStart();

	if((ret = it9175_waitTuning(pDev, FSUSB2I_TUNING_WAIT)) < 0) return FALSE;
	//# ignore check empty channel
	//# ignore check TS sync lock

	PurgeTsStream();

	return TRUE;
}

const DWORD CBonTuner::GetCurSpace(void)
{ return m_dwCurSpace; }

const DWORD CBonTuner::GetCurChannel(void)
{ return m_dwCurChannel; }


void CBonTuner::LoadValues(const IValueLoader *Loader)
{
	CBonFSHybrid::LoadValues (Loader) ;
	#define LOADDW(val) do { val = Loader->ReadDWORD(L#val,val); } while(0)
	LOADDW(FSUSB2I_SETFREQ_TIMES);
	LOADDW(FSUSB2I_TUNING_WAIT);
    LOADDW(FSUSB2I_LOCK_ON_SIGNAL);
	#undef LOADDW
}

} // End of namespace FSUSB2i

/*EOF*/
