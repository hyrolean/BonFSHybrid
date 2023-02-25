/* SunPTV-USB   (c) 2016 trinity19683
  BonTuner.DLL (MS-Windows)
  BonTuner.cpp
  2016-02-10
*/
#include "stdafx.h"

#include <tchar.h>
#include "BonTuner_uSUNpTV.h"
#include "../usbdevfile.h"
extern "C" {
#include "../tc90522.h"
#include "../tda20142.h"
#include "../mxl136.h"
}

using namespace std ;

namespace uSUNpTV {

DWORD USUNPTV_SETSFREQ_TIMES  = 1   ;
DWORD USUNPTV_SETSTSID_TIMES  = 2   ;
DWORD USUNPTV_SETTFREQ_TIMES  = 1   ;
DWORD USUNPTV_SETSLOCK_WAIT   = 10  ;
DWORD USUNPTV_SETSTSID_WAIT   = 800 ;
DWORD USUNPTV_CHANNEL_WAIT    = 480 ;
BOOL  USUNPTV_LOCK_ON_SIGNAL  = TRUE;

const TCHAR* const g_RegKey = TEXT("Software\\trinity19683\\FSUSB2i");

CBonTuner::CBonTuner()
: CBonFSHybrid(true), m_dwCurSpace(123), m_dwCurChannel(0), m_hasStream(FALSE),
 m_hDev(NULL), m_hUsbDev(NULL),pDev(NULL), demodDev(NULL), m_selectedTuner(-1),
 m_chCur()
{
	fill_n(tunerDev, 2, (void*)NULL);
}

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
	if(sscanf_s( upper_case(file_prefix_of(ModuleFileName())).c_str() , "BONDRIVER_USUNPTV_DEV%d", &idx )==1)
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
		if(!FindDevice(GUID_WINUSB_US3POUT_DRV,m_hDev,m_hUsbDev)) {
		  if(m_hDev==NULL) throw (const DWORD)__LINE__;
		  if(m_hUsbDev==NULL) throw (const DWORD)__LINE__;
		}
		//# device initialize
		m_USBEP.fd = m_hUsbDev;
		if( em287x_create(&pDev, &m_USBEP) ) throw (const DWORD)__LINE__;
		struct i2c_device_st* pI2C;
		//# demod
		if( tc90522_create(&demodDev) ) throw (const DWORD)__LINE__;
		pI2C = (struct i2c_device_st*)tc90522_i2c_ptr(demodDev);
		pI2C->addr = 0x20;
		em287x_attach(pDev, pI2C);
		if( tc90522_init(demodDev) ) throw (const DWORD)__LINE__;
		//# tuner 0 terra
		if( mxl136_create(&tunerDev[0]) ) throw (const DWORD)__LINE__;
		pI2C = (struct i2c_device_st*)mxl136_i2c_ptr(tunerDev[0]);
		pI2C->addr = 0xc0;
		tc90522_attach(demodDev, 0, pI2C);
		if( mxl136_init(tunerDev[0]) ) throw (const DWORD)__LINE__;
		//# tuner 1 BS/CS
		if( tda20142_create(&tunerDev[1]) ) throw (const DWORD)__LINE__;
		pI2C = (struct i2c_device_st*)tda20142_i2c_ptr(tunerDev[1]);
		pI2C->addr = 0xa8;
		tc90522_attach(demodDev, 1, pI2C);
		if( tda20142_init(tunerDev[1]) ) throw (const DWORD)__LINE__;
		//# demod set params
		if( tc90522_selectDevice(demodDev, 1) ) throw (const DWORD)__LINE__;
		//# fifo
		if(!FifoInitialize(&m_USBEP)) throw (const DWORD)__LINE__;

		//# device has been ready.

		DBGOUT("-*- device has been ready -*-\n") ;
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
	FifoFinalize() ;
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
	FreeDevice(m_hDev,m_hUsbDev);
	m_USBEP.dev=NULL;
	m_chCur = CHANNEL();
}

const float CBonTuner::GetSignalLevel(void)
{
	if(0 > m_selectedTuner || (! demodDev) ) return -3.1f;
	if(!m_hasStream) return 0.f ;
	unsigned statData[4];
	float lv ;

	const bool do_locking = USUNPTV_LOCK_ON_SIGNAL && m_USBEP.dev && m_USBEP.lockunlockFunc ;

	if(do_locking) //# lock
		m_USBEP.lockunlockFunc(m_USBEP.dev,1);

	if(tc90522_readStatistic(demodDev, m_selectedTuner, statData) )
		lv = -3.2f;
	else
		lv = statData[1] * 0.01f;

	if(do_locking) //# unlock
		m_USBEP.lockunlockFunc(m_USBEP.dev,0);

	return lv ;
}

void CBonTuner::Release()  //# release the instance
{ delete this; }

LPCTSTR CBonTuner::GetTunerName(void)
{ return TEXT("uSUNpTV"); }

const BOOL CBonTuner::IsTunerOpening(void)
{ return m_hUsbDev ? TRUE : FALSE; }

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	int tunerNum = 0;
	BOOL hasStream = TRUE;

	CHANNEL ch = GetChannel(dwSpace,dwChannel) ;

	if(ch.Band==BAND_na || ch.Freq < 60000 || ch.Freq > 2456123 ) {
		warn_msg(0,"BonDriver_uSUNpTV:SetChannel(%u,%u) invalid!", dwSpace, dwChannel);
		return FALSE;
	}
	if( ch.Freq >= 900000 ) tunerNum = 1;

	//# change channel
	m_hasStream=FALSE ;
	FifoStop() ;

	if(tunerNum != m_selectedTuner) {
		if( tc90522_selectDevice(demodDev, tunerNum) ) return FALSE;
		if(tunerNum & 0x1) {
			mxl136_sleep(tunerDev[0]);
		}else{
			mxl136_wakeup(tunerDev[0]);
			::Sleep( 30 );
		}
	}
	if(tunerNum & 0x1) {
		for(DWORD n=USUNPTV_SETSFREQ_TIMES;n;n--)
		if( m_chCur.Band!=ch.Band || m_chCur.Freq != ch.Freq ) {
			unsigned fail=0 ;
			if( tda20142_setFreq(tunerDev[1], ch.Freq) ) fail++ ;
			::Sleep( 30 );
			if( !fail && tc90522_resetDemod(demodDev, tunerNum ) ) fail++ ;
			if(n>1) ::Sleep( 50 );
			if(fail&&n==1) return FALSE;
		}
		for(DWORD n=USUNPTV_SETSTSID_TIMES;n;n--) {
			hasStream = FALSE;
			unsigned fail=0, lock=0 ;
			for(DWORD e=0,s=Elapsed();USUNPTV_SETSTSID_WAIT>e;e=Elapsed(s)) {
				int ret ;
				if(!lock) {
					unsigned data[2] ;
					ret =tc90522_readStatistic(demodDev, tunerNum, data);
					if(0 == ret) {
						lock = data[0]&0x10 ;  //# check lock bit
						if(lock) ::Sleep(USUNPTV_SETSLOCK_WAIT) ;
					}
				}else {
					if(ch.TSID>0)
						ret = tc90522_setTSID(demodDev, tunerNum, ch.TSID );
					else
						ret = tc90522_selectStream(demodDev, tunerNum, ch.Stream );
					if(0 == ret) { hasStream = TRUE; break; }
					else if(0 > ret) fail++ ;
				}
				::Sleep( 40 );
			}
			if(n>1&&hasStream)
				::Sleep( 40 );
			if(fail&&n==1) return FALSE;
		}
	}else for(DWORD n=USUNPTV_SETTFREQ_TIMES;n;n--) {
		unsigned fail=0 ;
		if( mxl136_setFreq(tunerDev[0], ch.Freq) ) fail++ ;
		::Sleep( 30 );
		if( !fail && tc90522_resetDemod(demodDev, tunerNum ) ) fail++ ;
		::Sleep( 50 );
		if(fail&&n==1) return FALSE;
	}
	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	m_hasStream = hasStream ;
	m_selectedTuner = tunerNum;
	m_chCur = ch ;

	if(hasStream) FifoStart() ;

	for(DWORD e=0,s=Elapsed();USUNPTV_CHANNEL_WAIT>e;e=Elapsed(s)) {
		unsigned statData[4];
		::Sleep( 40 );
		if( tc90522_readStatistic(demodDev, tunerNum, statData) ) continue;
		if( statData[0] & 0x10 ) break;
	}

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
	LOADDW(USUNPTV_SETSFREQ_TIMES);
	LOADDW(USUNPTV_SETSTSID_TIMES);
	LOADDW(USUNPTV_SETTFREQ_TIMES);
	LOADDW(USUNPTV_SETSLOCK_WAIT);
	LOADDW(USUNPTV_SETSTSID_WAIT);
	LOADDW(USUNPTV_CHANNEL_WAIT);
    LOADDW(USUNPTV_LOCK_ON_SIGNAL);
	#undef LOADDW
}

} // End of namespace uSUNpTV

/*EOF*/
