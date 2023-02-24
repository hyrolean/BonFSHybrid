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

// 有効にするとtrinity19683さん直伝のキャッシュ方式に変更
BOOL TSCACHING_LEGACY = FALSE ;
// キャッシュを整合化するかどうか（安定するが多少負荷がかかる）
BOOL TSCACHING_DEFRAGMENT = FALSE ;
// キャッシュを整合化する場合のパケットサイズ
DWORD TSCACHING_DEFRAGSIZE = 128*1024 ; // 128K(Spinelに最適)

DWORD ASYNCTS_QUEUENUM    = 66  ; // Default 3M (47K*66) bytes
DWORD ASYNCTS_QUEUEMAX    = 660 ; // Maximum growth 30M (47K*660) bytes
DWORD ASYNCTS_EMPTYBORDER = 22  ; // Empty border at least 1M (47K*22) bytes
DWORD ASYNCTS_EMPTYLIMIT  = 11  ; // Empty limit at least 0.5M (47K*11) bytes
#define TSTHREADWAIT        TS_PollTimeout
#define TSALLOCWAITING      false
#define TSALLOCMODERATE     true

DWORD TUNING_SETSFREQ_TIMES = 1   ;
DWORD TUNING_SETSTSID_TIMES = 2   ;
DWORD TUNING_SETTFREQ_TIMES = 1   ;
DWORD TUNING_SETSTSID_WAIT  = 800 ;
DWORD TUNING_CHANNEL_WAIT   = 480 ;

//# initialize static member variables
CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;

const TCHAR* const g_RegKey = TEXT("Software\\trinity19683\\FSUSB2i");

CBonTuner::CBonTuner()
: m_dwCurSpace(123), m_dwCurChannel(0), m_hDev(NULL), m_hUsbDev(NULL), pDev(NULL), demodDev(NULL), m_selectedTuner(-1), tsthr(NULL),
 m_ChannelList(NULL), m_bandCur(BAND_na), m_freqCur(0), m_streamCur(0), m_tsidCur(0)
{
	m_fifo=NULL ;
	ZeroMemory(m_mapCache,sizeof(m_mapCache)) ;
	int i;
	m_pThis = this;
	for(i = 0; i < 2; i++ ) {
		tunerDev[i] = NULL;
	}
	LoadData();
}

CBonTuner::~CBonTuner()
{
	CloseTuner();

	if(m_ChannelList != NULL) ::GlobalFree(m_ChannelList);
	m_pThis = NULL;
}

void CBonTuner::LoadUserChannels()
{
  string chFName = file_path_of(ModuleFileName()) + file_prefix_of(ModuleFileName()) + ".ch.txt" ;

  m_UserChannels.clear() ;
  m_UserSpaceIndices.clear() ;

  FILE *st=NULL ;
  fopen_s(&st,chFName.c_str(),"rt") ;
  if(!st) return ;
  char s[512] ;

  std::wstring space_name=L"" ;
  while(!feof(st)) {
	s[0]='\0' ;
	fgets(s,512,st) ;
	string strLine = trim(string(s)) ;
	if(strLine.empty()) continue ;
	wstring wstrLine = mbcs2wcs(strLine) ;
	int t=0 ;
	vector<wstring> params ;
	split(params,wstrLine,L';') ;
	wstrLine = params[0] ; params.clear() ;
	split(params,wstrLine,L',') ;
	if(params.size()>=2&&!params[0].empty()) {
	  BAND band = BAND_na ;
	  int channel = 0 ;
	  DWORD freq = 0 ;
	  int stream = 0 ;
	  int tsid = 0 ;
	  wstring &space = params[0] ;
	  wstring name = params.size()>=3 ? params[2] : wstring(L"") ;
	  wstring subname = params[1] ;
	  vector<wstring> phyChDiv ;
	  split(phyChDiv,params[1],'/') ;
	  for(size_t i=0;i<phyChDiv.size();i++) {
		wstring phyCh = phyChDiv[i] ;
		if( phyCh.length()>3&&
			phyCh.substr(phyCh.length()-3)==L"MHz" ) {
		  float megaHz = 0.f ;
		  if(swscanf_s(phyCh.c_str(),L"%fMHz",&megaHz)==1) {
			freq=DWORD(megaHz*1000.f) ;
			channel = CHANNEL::BandFromFreq(freq)!=BAND_na ? -1 : 0 ;
		  }
		}else {
		  if(swscanf_s(phyCh.c_str(),L"TS%d",&stream)==1)
			;
		  else if(swscanf_s(phyCh.c_str(),L"ID%i",&tsid)==1)
			;
		  else if(band==BAND_na) {
			if(swscanf_s(phyCh.c_str(),L"BS%d",&channel)==1)
			  band = BAND_BS ;
			else if(swscanf_s(phyCh.c_str(),L"ND%d",&channel)==1)
			  band = BAND_ND ;
			else if(swscanf_s(phyCh.c_str(),L"C%d",&channel)==1)
			  band = BAND_VU, subname=L"C"+itows(channel)+L"ch", channel+=100 ;
			else if(swscanf_s(phyCh.c_str(),L"%d",&channel)==1)
			  band = BAND_VU, subname=itows(channel)+L"ch" ;
		  }
		}
	  }
	  if(name==L"")
		name=subname ;
	  if(freq>0&&channel<0)
		m_UserChannels.push_back(
		  CHANNEL(space,freq,name,stream,tsid)) ;
	  else if(band!=BAND_na&&channel>0)
		m_UserChannels.push_back(
		  CHANNEL(space,band,channel,name,stream,tsid)) ;
	  else
		continue ;
	  if(space_name!=space) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()-1) ;
		space_name=space ;
	  }
	}
  }

  fclose(st) ;
}

int CBonTuner::UserDecidedDeviceIdx()
{
	int idx=0 ;
	if(sscanf_s( upper_case(file_prefix_of(ModuleFileName())).c_str() , "BONDRIVER_USUNPTV_DEV%d", &idx )==1)
	  return idx ;

	return -1 ;
}

string CBonTuner::ModuleFileName()
{
	char path[_MAX_PATH] ;
	GetModuleFileNameA( m_hModule, path, _MAX_PATH ) ;
	return path ;
}


const BOOL CBonTuner::OpenTuner()
{
	//# if already open, close tuner
	CloseTuner();
	if(IsTunerOpening()) return FALSE;

	try{
		//# AllocTuner
		for(int idx = UserDecidedDeviceIdx(); idx < 40;) {
			HANDLE hDev;
			if((hDev = usbdevfile_alloc(&idx,&GUID_WINUSB_US3POUT_DRV) ) == NULL) {   //# not found
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
		//# fifo
		write_back_t *pwback = NULL ;
		write_back_t wback={0} ;
		if(!TSCACHING_LEGACY) {
			if(m_eoCaching.is_valid()) {
				size_t TSDATASIZE = ((m_USBEP.xfer_size+TS_DeltaSize+0x1FFUL)&~0x1FFUL) ;
				if(TSCACHING_DEFRAGMENT)
					TSDATASIZE = max(TSCACHING_DEFRAGSIZE,TSDATASIZE);
				DBGOUT("TSDATASIZE=%d\n",TSDATASIZE) ;
				m_fifo = new CAsyncFifo(
					ASYNCTS_QUEUENUM,ASYNCTS_QUEUEMAX,ASYNCTS_EMPTYBORDER,
					TSDATASIZE,TSTHREADWAIT ) ;
				if(m_fifo) {
					m_fifo->SetEmptyLimit(ASYNCTS_EMPTYLIMIT) ;
					m_fifo->SetModerateAllocating(TSALLOCMODERATE);
					wback.begin_func = &OnWriteBackBegin ;
					wback.finish_func = &OnWriteBackFinish ;
					wback.purge_func = &OnWriteBackPurge ;
					wback.arg = this ;
					pwback = &wback ;
				}
			}
		}
		//# TS receive thread
		if( tsthread_create(&tsthr, &m_USBEP, pwback) ) {
			throw (const DWORD)__LINE__;
		}

		//# device has been ready.
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
	if(m_fifo) {
		delete m_fifo ;
		m_fifo = NULL ;
	}
	m_bandCur = BAND_na ;
	m_freqCur = 0 ;
	m_streamCur = 0 ;
}

const BOOL CBonTuner::SetChannel(const BYTE bCh)
{
	if(UserChannelExists()) return FALSE ;
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
	if(m_fifo) {
		if(m_fifo->Size()>0) return WAIT_OBJECT_0 ;
		return m_eoCaching.wait(remainTime) ;
	}
	const int r = tsthread_wait(tsthr, remainTime);
	if(0 > r)  return WAIT_FAILED;
	else if(0 < r)  return WAIT_OBJECT_0;
	else  return WAIT_TIMEOUT;
}

const DWORD CBonTuner::GetReadyCount()
{//# number of call GetTsStream()
	if(! tsthr) return 0;
	if(m_fifo) return static_cast<DWORD>(m_fifo->Size());
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
	if(m_fifo) {
		m_fifo->Pop(ppDst,pdwSize,pdwRemain);
		return TRUE ;
	}
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
{ return TEXT("US-3POUT"); }

const BOOL CBonTuner::IsTunerOpening(void)
{ return m_hUsbDev ? TRUE : FALSE; }

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	if(UserChannelExists()) {
		if(dwSpace<m_UserSpaceIndices.size())
			return m_UserChannels[m_UserSpaceIndices[dwSpace]].Space.c_str() ;
		return NULL ;
	}
	if(0 == dwSpace)  return TEXT("地デジ");
	else if(1 == dwSpace)  return TEXT("BS");
	else if(2 == dwSpace)  return TEXT("CS");
	return NULL;
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if(UserChannelExists()) {
	  if(dwSpace<m_UserSpaceIndices.size()) {
		  DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
		  DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size() ?
			  m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
		  if(dwChannel<end-begin)
			  return m_UserChannels[begin+dwChannel].Name.c_str() ;
	  }
	  return NULL ;
	}

	static TCHAR buf[6];
	if(0 == dwSpace) {
		if(m_ChannelList != NULL) {
			//# User-defined channels
			const DWORD dwChannelLen    = m_ChannelList[0] >> 16;
			const DWORD dwNumOfChannels = m_ChannelList[0] & 0xFFFF;
			TCHAR* const ptrStr = (TCHAR*)(m_ChannelList + dwNumOfChannels + 1);
			if(dwChannel < dwNumOfChannels) return ptrStr + (dwChannelLen * dwChannel);
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
	unsigned stream = 0 ;
	WORD tsid = 0;
	BOOL hasStream = TRUE;

	BAND band = BAND_na ;

	if(UserChannelExists()){
		if(dwSpace<m_UserSpaceIndices.size()) {
			DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
			DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size()
				? m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
			if(dwChannel<end-begin) {
				DWORD index = begin + dwChannel ;
				band    = m_UserChannels[index].Band ;
				dwFreq  = m_UserChannels[index].Freq ;
				stream  = m_UserChannels[index].Stream ;
				tsid    = m_UserChannels[index].TSID ;
			}
		}
	}else {
		if(0 == dwSpace) {
			if(m_ChannelList != NULL) {  //# User-defined channels
				const DWORD dwNumOfChannels = m_ChannelList[0] & 0xFFFF;
				if(dwChannel < dwNumOfChannels) {
					dwFreq = m_ChannelList[dwChannel + 1];
					band = CHANNEL::BandFromFreq(dwFreq) ;
				}
			}else{  //# UHF standard channels
				if(dwChannel < 40) {
					dwFreq = dwChannel * 6000 + 473143;
					band = BAND_VU ;
				}
			}
		}else if(1 == dwSpace && dwChannel < 12 * 8) {
			dwFreq = (dwChannel >> 3) * 38360 + 1049480;
			band = BAND_BS ;
			stream = dwChannel&7 ;
		}else if(2 == dwSpace && dwChannel < 12 * 8) {
			dwFreq = (dwChannel >> 3) * 40000 + 1613000;
			band = BAND_ND ;
			stream = dwChannel&7 ;
		}else if(dwSpace == 114514) {  //# dwChannel as freq/kHz
			dwFreq = dwChannel;
			band = CHANNEL::BandFromFreq(dwFreq) ;
		}
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
			::Sleep( 30 );
		}
	}
	if(tunerNum & 0x1) {
		for(DWORD n=TUNING_SETSFREQ_TIMES;n;n--)
		if( m_bandCur!=band || m_freqCur != dwFreq ) {
			unsigned fail=0 ;
			if( tda20142_setFreq(tunerDev[1], dwFreq) ) fail++ ;
			::Sleep( 30 );
			if( !fail && tc90522_resetDemod(demodDev, tunerNum ) ) fail++ ;
			//::Sleep( 50 );
			if(fail&&n==1) return FALSE;
		}
		for(DWORD n=TUNING_SETSTSID_TIMES;n;n--) {
			hasStream = FALSE;
			unsigned fail=0, lock=0 ;
			for(DWORD e=0,s=Elapsed();TUNING_SETSTSID_WAIT>e;e=Elapsed(s)) {
				int ret ;
				if(!lock) {
					unsigned data[2] ;
					ret =tc90522_readStatistic(demodDev, tunerNum, data);
					if(0 == ret) {
						lock = data[0]&0x10 ;  //# check lock bit
						if(lock) ::Sleep(10) ;
					}
				}else {
					if(tsid>0)
						ret = tc90522_setTSID(demodDev, tunerNum, tsid );
					else
						ret = tc90522_selectStream(demodDev, tunerNum, stream );
					if(0 == ret) { hasStream = TRUE; break; }
					else if(0 > ret) fail++ ;
				}
				::Sleep( 40 );
			}
			if(n>1&&hasStream)
				::Sleep( 40 );
			if(fail&&n==1) return FALSE;
		}
	}else for(DWORD n=TUNING_SETTFREQ_TIMES;n;n--) {
		unsigned fail=0 ;
		if( mxl136_setFreq(tunerDev[0], dwFreq) ) fail++ ;
		::Sleep( 30 );
		if( !fail && tc90522_resetDemod(demodDev, tunerNum ) ) fail++ ;
		::Sleep( 50 );
		if(fail&&n==1) return FALSE;
	}
	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	m_selectedTuner = tunerNum;
	m_bandCur = band;
	m_freqCur = dwFreq;
	m_streamCur = stream;
	m_tsidCur = tsid;

	if(tsthr && hasStream) tsthread_start(tsthr);

	for(DWORD e=0,s=Elapsed();TUNING_CHANNEL_WAIT>e;e=Elapsed(s)) {
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


bool CBonTuner::LoadData ()
{
	HKEY hKey;

	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, g_RegKey, 0, KEY_READ, &hKey)) {
		ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}
	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, g_RegKey, 0, KEY_READ, &hKey)) {
		ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}

	LoadUserChannels() ;

	return true;
}

	static DWORD RegReadDword(HKEY hKey,LPCTSTR name,DWORD defVal=0) {
		BYTE buf[sizeof DWORD] ;
		DWORD rdSize = sizeof DWORD ;
		DWORD type = 0 ;
		if(ERROR_SUCCESS==RegQueryValueEx(
		  hKey, name, 0, &type, buf, &rdSize )) {
			if(type==REG_DWORD) {
				DWORD result = *(DWORD*)(&buf[0]) ;
				//TRACE(L"Mode: %s=%d\n",name,result) ;
				DBGOUT("Mode: %s=%d\n",wcs2mbcs(name).c_str(),result);
				return result ;
			}
		}
		return defVal ;
	}

void CBonTuner::ReadRegMode (HKEY hPKey)
{
	#define LOADDW(val) do { val = RegReadDword(hPKey,L#val,val); } while(0)
	LOADDW(TSCACHING_LEGACY);
	LOADDW(TSCACHING_DEFRAGMENT);
	LOADDW(TSCACHING_DEFRAGSIZE);
	LOADDW(ASYNCTS_QUEUENUM);
	LOADDW(ASYNCTS_QUEUEMAX);
	LOADDW(ASYNCTS_EMPTYBORDER);
	LOADDW(ASYNCTS_EMPTYLIMIT);
	LOADDW(TUNING_SETSFREQ_TIMES);
	LOADDW(TUNING_SETSTSID_TIMES);
	LOADDW(TUNING_SETTFREQ_TIMES);
	LOADDW(TUNING_SETSTSID_WAIT) ;
	LOADDW(TUNING_CHANNEL_WAIT) ;
	#undef LOADDW
}

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

void *CBonTuner::OnWriteBackBegin(int id, size_t max_size, void *arg)
{
	if(id<0||id>=TS_MaxNumIO) return NULL ;
	CBonTuner *tuner = static_cast<CBonTuner*>(arg) ;
	CAsyncFifo::CACHE *cache = tuner->m_fifo->BeginWriteBack(TSALLOCWAITING) ;
	if(!cache) return NULL ;
	else cache->resize(max_size) ;
	tuner->m_coPurge.lock() ;
	tuner->m_mapCache[id] = cache ;
	tuner->m_coPurge.unlock() ;
	return cache->data() ;
}

void CBonTuner::OnWriteBackFinish(int id, size_t wrote_size, void *arg)
{
	CBonTuner *tuner = static_cast<CBonTuner*>(arg) ;
	exclusive_lock purgeLock(&tuner->m_coPurge);
	CAsyncFifo::CACHE *cache = tuner->m_mapCache[id] ;
	if(cache) {
		tuner->m_mapCache[id]=NULL ;
		purgeLock.unlock();
		if(TSCACHING_DEFRAGMENT) {
			if (tuner->m_fifo->Push(cache->data(), static_cast<DWORD>(wrote_size), false, TSALLOCWAITING) > 0)
			{ tuner->m_eoCaching.set(); cache->resize(0); }
			if (tuner->m_fifo->FinishWriteBack(cache,true))
			  tuner->m_eoCaching.set();
		}else {
			cache->resize(wrote_size) ;
			if(tuner->m_fifo->FinishWriteBack(cache))
				tuner->m_eoCaching.set() ;
		}
	}
}

void CBonTuner::OnWriteBackPurge(void *arg)
{
	CBonTuner *tuner = static_cast<CBonTuner*>(arg) ;
	exclusive_lock purgeLock(&tuner->m_coPurge);
	ZeroMemory(tuner->m_mapCache,sizeof(tuner->m_mapCache));
	tuner->m_fifo->Purge(true) ;
}

} // End of namespace uSUNpTV

/*EOF*/