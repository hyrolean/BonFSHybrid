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

DWORD USUNPTV_SETSFREQ_TIMES  = 1     ;
DWORD USUNPTV_SETSTSID_TIMES  = 2     ;
DWORD USUNPTV_SETTFREQ_TIMES  = 1     ;
DWORD USUNPTV_SETSLOCK_WAIT   = 10    ;
DWORD USUNPTV_SETSTSID_WAIT   = 800   ;
DWORD USUNPTV_CHANNEL_WAIT    = 800   ;
BOOL  USUNPTV_LOCK_ON_SIGNAL  = TRUE  ;
BOOL  USUNPTV_FASTSCAN        = FALSE ;

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

const BOOL CBonTuner::TryOpenTuner()
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
		DBGOUT("BonDriver_uSUNpTV::OpenTuner(Line: %u): failed.\n", dwErrorStep);
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
	m_selectedTuner = -1 ;
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

BOOL CBonTuner::select_ch(const CHANNEL &ch, BOOL doSetFreq, BOOL doSetTSID)
{
	BOOL hasStream = TRUE ;
	int tunerNum = 0;

	if(ch.Band==BAND_na) {
		warn_msg(0,"BonDriver_uSUNpTV:doSetChannel(BAND=na) invalid!");
		return FALSE;
	}

	if(ch.Freq < 60000 || ch.Freq > 2456123 ) {
		warn_msg(0,"BonDriver_uSUNpTV:doSetChannel(Freq=%d) invalid!", ch.Freq);
		return FALSE;
	}
	if( ch.Freq >= 900000 ) tunerNum = 1;


	if(tunerNum != m_selectedTuner) {
		if( tc90522_selectDevice(demodDev, tunerNum) ) return FALSE;
		if(tunerNum & 0x1) {
			mxl136_sleep(tunerDev[0]);
		}else{
			mxl136_wakeup(tunerDev[0]);
			HRSleep( 30 );
		}
		m_selectedTuner = tunerNum;
	}
	if(tunerNum & 0x1) {
		if(doSetFreq) {
			for(DWORD n=USUNPTV_SETSFREQ_TIMES;n;n--)
			if( m_chCur.Band!=ch.Band || m_chCur.Freq != ch.Freq ) {
				unsigned fail=0 ;
				if( tda20142_setFreq(tunerDev[1], ch.Freq) ) fail++ ;
				HRSleep( 30 );
				if( !fail && tc90522_resetDemod(demodDev, tunerNum ) ) fail++ ;
				if(n>1) HRSleep( 50 );
				if(fail&&n==1) return FALSE;
			}
		}
		if(doSetTSID) for(DWORD n=USUNPTV_SETSTSID_TIMES;n;n--) {
			hasStream = FALSE;
			unsigned fail=0, lock=0 ;
			for(DWORD e=0,s=Elapsed();USUNPTV_SETSTSID_WAIT>e;e=Elapsed(s)) {
				int ret ;
				if(!lock) {
					unsigned data[2] ;
					ret =tc90522_readStatistic(demodDev, tunerNum, data);
					if(0 == ret) {
						lock = data[0]&0x10 ;  //# check lock bit
						if(lock) HRSleep(USUNPTV_SETSLOCK_WAIT) ;
					}
				}else {
					if(ch.TSID>0)
						ret = tc90522_setTSID(demodDev, tunerNum, ch.TSID );
					else
						ret = tc90522_selectStream(demodDev, tunerNum, ch.Stream );
					if(0 == ret) { hasStream = TRUE; break; }
					else if(0 > ret) fail++ ;
				}
				HRSleep( 40 );
			}
			if(n>1&&hasStream)
				HRSleep( 40 );
			if(fail&&n==1) return FALSE;
		}
	}else if(doSetFreq) for(DWORD n=USUNPTV_SETTFREQ_TIMES;n;n--) {
		unsigned fail=0 ;
		if( mxl136_setFreq(tunerDev[0], ch.Freq) ) fail++ ;
		HRSleep( 30 );
		if( !fail && tc90522_resetDemod(demodDev, tunerNum ) ) fail++ ;
		HRSleep( 50 );
		if(fail&&n==1) return FALSE;
	}

	if(hasStream && doSetTSID) {
		hasStream=FALSE;
		for(DWORD e=0,s=Elapsed();USUNPTV_CHANNEL_WAIT>e;e=Elapsed(s)) {
			unsigned statData[4];
			HRSleep( 40 );
			if( tc90522_readStatistic(demodDev, tunerNum, statData) ) continue;
			if( statData[0] & 0x10 ) { hasStream=TRUE; break; }
		}
	}

	return hasStream ;
}


const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	BOOL hasStream = TRUE;

	CHANNEL ch = GetChannel(dwSpace,dwChannel) ;

	//# change channel
	m_hasStream=FALSE ;
	FifoStop() ;

	hasStream = select_ch(ch) ;

	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	m_chCur = ch ;
	m_hasStream = hasStream ;

	if(hasStream) FifoStart() ;

	PurgeTsStream();

	return USUNPTV_FASTSCAN ? hasStream : TRUE;
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
	LOADDW(USUNPTV_FASTSCAN);
	#undef LOADDW
}

  // IBonTransponder

const BOOL CBonTuner::TransponderSelect(const DWORD dwSpace, const DWORD dwTransponder)
{
  if(!IsTunerOpening()) return FALSE;

  int idx = transponder_index_of(dwSpace, dwTransponder) ;
  if(idx<0) return FALSE ;

  BOOL res = select_ch(m_Transponders[idx], TRUE, FALSE) ;

  if(res) {
    m_dwCurSpace = dwSpace;
    m_dwCurChannel = dwTransponder | TRANSPONDER_CHMASK ;
    m_chCur = m_Transponders[idx] ;
    m_hasStream = FALSE; // TransponderSetCurID ‚Í‚Ü‚¾s‚Á‚Ä‚¢‚È‚¢‚Ì‚Å
  }

  return res ;
}

const BOOL CBonTuner::TransponderGetIDList(LPDWORD lpIDList, LPDWORD lpdwNumID)
{
  if(!IsTunerOpening()) return FALSE;
  if(m_chCur.Band!=BAND_BS&&m_chCur.Band!=BAND_ND) return FALSE;

  const DWORD numId = 8 ;

  if(lpdwNumID==NULL) {
    return FALSE ;
  }else if(lpIDList==NULL) {
    *lpdwNumID = numId ;
    return TRUE ;
  }

  uint8_t tmcc[44] ;
  if(tc90522_readTMCC(demodDev, 1, tmcc)) return FALSE;

  DWORD num = min(8,*lpdwNumID) ;
  uint8_t *p = tmcc + 28 ;
  for(DWORD i=0;i<num;i++) {
    DWORD id = *p++ ; id<<=8 ; id |= *p++ ;
	if(id==0||id==0xffff) id = 0xFFFFFFFF ;
	lpIDList[i] = id ;
  }
  *lpdwNumID = num ;

  return TRUE ;
}

const BOOL CBonTuner::TransponderSetCurID(const DWORD dwID)
{
  if(!IsTunerOpening()) return FALSE;
  if(m_chCur.Band!=BAND_BS&&m_chCur.Band!=BAND_ND) return FALSE;

  //# change channel
  m_hasStream=FALSE ;
  FifoStop() ;

  CHANNEL ch = m_chCur ;
  ch.TSID = (WORD) (dwID&0xFFFF) ;
  BOOL res = select_ch(ch, FALSE, TRUE) ;

  if(res) {
    m_chCur = ch ;
    m_hasStream = TRUE ;
    FifoStart() ;
  }

  PurgeTsStream();

  return res ;
}

const BOOL CBonTuner::TransponderGetCurID(LPDWORD lpdwID)
{
  if(!IsTunerOpening()) return FALSE;
  if(m_chCur.Band!=BAND_BS&&m_chCur.Band!=BAND_ND) return FALSE;

  if(!m_hasStream) {
    *lpdwID=0xFFFFFFFF;
    return TRUE;
  }

  uint8_t tmcc[44] ;
  if(tc90522_readTMCC(demodDev, 1, tmcc)) return FALSE;

  uint8_t *p = tmcc + 2 ;
  DWORD id = *p++ ; id<<=8 ; id |= *p++ ;
  if(id==0||id==0xffff) id = 0xFFFFFFFF ;
  *lpdwID = id ;

  return TRUE;
}

} // End of namespace uSUNpTV

/*EOF*/
