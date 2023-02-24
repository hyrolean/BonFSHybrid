//===========================================================================
#include "stdafx.h"
//---------------------------------------------------------------------------

#include <string>
#include "bonhybrid.h"
#include "usbdevfile.h"
//---------------------------------------------------------------------------

using namespace std;

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

// Instance
CBonFSHybrid* CBonFSHybrid::m_pThis = NULL;
HINSTANCE CBonFSHybrid::m_hModule = NULL;

//===========================================================================
// Functions
//---------------------------------------------------------------------------
DWORD RegReadDword(HKEY hKey,LPCTSTR name,DWORD defVal)
{
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
//===========================================================================
// CBonFSHybrid
//---------------------------------------------------------------------------
CBonFSHybrid::CBonFSHybrid()
{
	m_pThis = this;
	m_RegChannels = NULL;
	m_hasSatellite=false ;
	tsthr=NULL ;
	m_fifo=NULL ;
	FifoFinalize() ;
}
//---------------------------------------------------------------------------
CBonFSHybrid::~CBonFSHybrid()
{
	FifoFinalize() ;
	if(m_RegChannels != NULL) ::GlobalFree(m_RegChannels);
	m_pThis = NULL;
}
//---------------------------------------------------------------------------
string CBonFSHybrid::ModuleFileName()
{
	char path[_MAX_PATH] ;
	GetModuleFileNameA( m_hModule, path, _MAX_PATH ) ;
	return path ;
}
//---------------------------------------------------------------------------
bool CBonFSHybrid::FindDevice(const GUID &drvGUID, HANDLE &hDev, HANDLE &hUsbDev)
{
	hDev=hUsbDev=NULL;
	int idx = UserDecidedDeviceIdx();

	if((hDev = usbdevfile_alloc(&idx,&drvGUID) ) == NULL) {   //# not found
		return false;
	}
	if((hUsbDev = usbdevfile_init(hDev) ) == NULL) {   //# failed
		return false;
	}
	return true ;
}
//---------------------------------------------------------------------------
void CBonFSHybrid::FreeDevice(HANDLE &hDev, HANDLE &hUsbDev)
{
	if(hUsbDev) {
		usbdevfile_free(hUsbDev);
		hUsbDev = NULL;
	}
	if(hDev) {
		::CloseHandle( hDev );
		hDev = NULL;
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::LoadUserChannels(bool hasSatellite)
{
  m_hasSatellite = hasSatellite ;
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
		  if(hasSatellite&&swscanf_s(phyCh.c_str(),L"TS%d",&stream)==1)
			;
		  else if(hasSatellite&&swscanf_s(phyCh.c_str(),L"ID%i",&tsid)==1)
			;
		  else if(band==BAND_na) {
			if(hasSatellite&&swscanf_s(phyCh.c_str(),L"BS%d",&channel)==1)
			  band = BAND_BS ;
			else if(hasSatellite&&swscanf_s(phyCh.c_str(),L"ND%d",&channel)==1)
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
//---------------------------------------------------------------------------
CBonFSHybrid::CHANNEL *CBonFSHybrid::GetUserChannel(DWORD dwSpace, DWORD dwChannel)
{
	if(UserChannelExists()){
		if(dwSpace<m_UserSpaceIndices.size()) {
			DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
			DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size()
				? m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
			if(dwChannel<end-begin) {
				DWORD index = begin + dwChannel ;
				return &m_UserChannels[index] ;
			}
		}
	}
	return NULL ;
}
//---------------------------------------------------------------------------
CBonFSHybrid::CHANNEL CBonFSHybrid::GetChannel(DWORD dwSpace, DWORD dwChannel)
{
	if(dwSpace == SPACE_CHASFREQ) {  //# dwChannel as freq/kHz
		return CHANNEL(L"CHASFREQ",dwChannel,itows(dwChannel)+L"kHz") ;
	}else if(UserChannelExists()) {
		if(CHANNEL *userChannel = GetUserChannel(dwSpace,dwChannel))
		    return *userChannel ;
	}else {
		LPCTSTR strSpace = EnumTuningSpace(dwSpace) ;
		LPCTSTR strChannel = EnumChannelName(dwSpace,dwChannel) ;
		if(strSpace&&strChannel) {
		   	const DWORD d = m_RegChannels ? 1 : 0 ;
			if(0==dwSpace||(d&&1==dwSpace))  //# VHS/UHF
				return CHANNEL(strSpace,GetTerraFreq(dwSpace,dwChannel),strChannel) ;
			else if(m_hasSatellite) {
				if(1+d == dwSpace && dwChannel < 12 * 8)  //# BS
					return CHANNEL(strSpace,BAND_BS,(dwChannel>>3)*2+1,strChannel,dwChannel&7) ;
				else if(2+d == dwSpace && dwChannel < 12 * 8)  //# CS
					return CHANNEL(strSpace,BAND_ND,(dwChannel>>3)*2+2,strChannel,dwChannel&7) ;

			}

		}
	}
	return CHANNEL() ;
}
//---------------------------------------------------------------------------
DWORD CBonFSHybrid::GetTerraFreq(DWORD dwSpace, DWORD dwChannel)
{
	if(dwSpace == 0) {//# UHF standard channels
		if(dwChannel < 40)
			return dwChannel * 6000 + 473143;
	}else if(dwSpace == 1 && m_RegChannels != NULL) {//# Defined channels on registry
		const DWORD dwNumOfChannels = m_RegChannels[0] & 0xFFFF;
		if(dwChannel < dwNumOfChannels)
			return m_RegChannels[dwChannel + 1];
	}
	return 0 ;
}
//---------------------------------------------------------------------------
bool CBonFSHybrid::FifoInitialize(usb_endpoint_st *usbep)
{
	write_back_t *pwback = NULL ;
	write_back_t wback={0} ;
	FifoFinalize();
	if(!TSCACHING_LEGACY) {
		if(m_eoCaching.is_valid()) {
			size_t TSDATASIZE = ((usbep->xfer_size+TS_DeltaSize+0x1FFUL)&~0x1FFUL) ;
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
	if( tsthread_create(&tsthr, usbep, pwback) ) {
		throw (const DWORD)__LINE__;
	}
	return m_fifo!=NULL ;  // true : general, false : legacy
}
//---------------------------------------------------------------------------
void CBonFSHybrid::FifoFinalize()
{
	if(tsthr) {
		tsthread_stop(tsthr);
		tsthread_destroy(tsthr);
		tsthr = NULL;
	}
	if(m_fifo) {
		delete m_fifo;
		m_fifo = NULL;
	}
	ZeroMemory(m_mapCache,sizeof(m_mapCache)) ;
}
//---------------------------------------------------------------------------
void CBonFSHybrid::FifoStart()
{
	if(tsthr) tsthread_start(tsthr);
}
//---------------------------------------------------------------------------
void CBonFSHybrid::FifoStop()
{
	if(tsthr) tsthread_stop(tsthr);
}
//---------------------------------------------------------------------------
void CBonFSHybrid::ReadRegMode(HKEY hPKey)
{
	#define LOADDW(val) do { val = RegReadDword(hPKey,L#val,val); } while(0)
	LOADDW(TSCACHING_LEGACY);
	LOADDW(TSCACHING_DEFRAGMENT);
	LOADDW(TSCACHING_DEFRAGSIZE);
	LOADDW(ASYNCTS_QUEUENUM);
	LOADDW(ASYNCTS_QUEUEMAX);
	LOADDW(ASYNCTS_EMPTYBORDER);
	LOADDW(ASYNCTS_EMPTYLIMIT);
	#undef LOADDW
}
//---------------------------------------------------------------------------
void CBonFSHybrid::ReadRegChannels(HKEY hPKey)
{
	if(m_RegChannels != NULL) return;

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
	m_RegChannels = (DWORD*) ::GlobalAlloc(GMEM_FIXED, NumOfValues * (dwMaxValueName * sizeof(TCHAR) + sizeof(DWORD)) + sizeof(DWORD) );
	m_RegChannels[0] = dwMaxValueName << 16 | NumOfValues;
	ZeroMemory( m_RegChannels + 1, sizeof(DWORD) * NumOfValues );
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
		m_RegChannels[dwByte + 1] = dwValue & 0x00ffffff;
		ptrStr = (TCHAR*)(m_RegChannels + NumOfValues + 1);
		ptrStr += dwMaxValueName * dwByte;
		lstrcpyn( ptrStr, szValueName, dwMaxValueName );
	}
	RegCloseKey(hKey);
}
//---------------------------------------------------------------------------
const BOOL CBonFSHybrid::SetChannel(const BYTE bCh)
{
	DWORD dwFreq = 0 ;
	if(UserChannelExists()) {
		if(size_t(bCh)<m_UserChannels.size()) {
			DWORD space = 0 ;
			for(;space<m_UserSpaceIndices.size();space++) {
				if(bCh<m_UserSpaceIndices[space]) {
				  break ;
				}
			}
			if(--space<m_UserSpaceIndices.size()) {
				DWORD ch = DWORD(bCh)-DWORD(m_UserSpaceIndices[space]) ;
				return SetChannel(space,ch) ;
			}
		}
	}else {
		if( (bCh>=13&&bCh<=62) || (bCh>=113&&bCh<=163) ) //# VU
			dwFreq = CHANNEL::FreqFromBandCh(BAND_VU,bCh) ;
		#if 0
		else if(m_hasSatellite&&bCh>=200&&bCh<=224) {
			if(bCh&1) //# BS ( odd )
				dwFreq = CHANNEL::FreqFromBandCh(BAND_BS,bCh-200) ;
			else //# ND ( even )
				dwFreq = CHANNEL::FreqFromBandCh(BAND_ND,bCh-200) ;
		}
		#endif
	}
	return SetChannel(SPACE_CHASFREQ, dwFreq);
}
//---------------------------------------------------------------------------
const DWORD CBonFSHybrid::WaitTsStream(const DWORD dwTimeOut)
{
	const int remainTime = (dwTimeOut < 0x10000000) ? dwTimeOut : 0x10000000;
	if(! tsthr) return WAIT_ABANDONED;
	if(m_fifo) {
		if(m_fifo->Size()>0) return WAIT_OBJECT_0 ;
		DWORD res = m_eoCaching.wait(remainTime) ;
		if(m_fifo->Empty()) return WAIT_TIMEOUT ;
		return res ;
	}
	const int r = tsthread_wait(tsthr, remainTime);
	if(0 > r)  return WAIT_ABANDONED;
	else if(0 < r)  return WAIT_OBJECT_0;
	else  return WAIT_TIMEOUT;
}
//---------------------------------------------------------------------------
const DWORD CBonFSHybrid::GetReadyCount()
{//# number of call GetTsStream()
	if(! tsthr) return 0;
	if(m_fifo) return static_cast<DWORD>(m_fifo->Size());
	const int ret = tsthread_readable(tsthr);
	return (ret > 0) ? 1 : 0;
}
//---------------------------------------------------------------------------
const BOOL CBonFSHybrid::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;
	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize) ::CopyMemory(pDst, pSrc, *pdwSize);
		return TRUE;
	}
	return FALSE;
}
//---------------------------------------------------------------------------
const BOOL CBonFSHybrid::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
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
//---------------------------------------------------------------------------
void CBonFSHybrid::PurgeTsStream()
{
	if(! tsthr) return;
	//# purge available data in TS buffer
	tsthread_read(tsthr, NULL);
}
//---------------------------------------------------------------------------
LPCTSTR CBonFSHybrid::EnumTuningSpace(const DWORD dwSpace)
{
	if(UserChannelExists()) {
		if(dwSpace<m_UserSpaceIndices.size())
			return m_UserChannels[m_UserSpaceIndices[dwSpace]].Space.c_str() ;
	}else {
		if(dwSpace==0) return TEXT("地デジ") ;
		else if(dwSpace==1&&m_RegChannels!=NULL) return TEXT("地デジ(追加)") ;
		else if(m_hasSatellite) {
			const DWORD d = m_RegChannels ? 1 : 0 ;
			if(1+d == dwSpace)  return TEXT("BS");
			else if(2+d == dwSpace)  return TEXT("CS");
		}
	}
	return NULL ;
}
//---------------------------------------------------------------------------
LPCTSTR CBonFSHybrid::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	static TCHAR buf[8];
	if(UserChannelExists()) {
	  if(dwSpace<m_UserSpaceIndices.size()) {
		  DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
		  DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size() ?
			  m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
		  if(dwChannel<end-begin)
			  return m_UserChannels[begin+dwChannel].Name.c_str() ;
	  }
	}else {
		if(dwSpace == 0) {
			if(dwChannel < 40) {
				_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%u"), dwChannel + 13);
				return buf;    //# The caller must copy data from this buffer.
			}
		}
		else if(dwSpace == 1 && m_RegChannels!=NULL) {
			//# Defined channels on registry
			const DWORD dwChannelLen    = m_RegChannels[0] >> 16;
			const DWORD dwNumOfChannels = m_RegChannels[0] & 0xFFFF;
			TCHAR* const ptrStr = (TCHAR*)(m_RegChannels + dwNumOfChannels + 1);
			if(dwChannel < dwNumOfChannels)	return ptrStr + (dwChannelLen * dwChannel);
		}
		else if(m_hasSatellite) {
			const DWORD d = m_RegChannels ? 1 : 0 ;
			if(1+d == dwSpace && dwChannel < 12 * 8) {
				_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%02u.%u"), (dwChannel >> 3)*2 + 1, dwChannel & 0x7);
				return buf;
			}else if(2+d == dwSpace && dwChannel < 12 * 8) {
				_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%02u.%u"), (dwChannel >> 3)*2 + 2, dwChannel & 0x7);
				return buf;
			}
		}
	}
	return NULL ;
}
//---------------------------------------------------------------------------
void *CBonFSHybrid::OnWriteBackBegin(int id, size_t max_size, void *arg)
{
	if(id<0||id>=TS_MaxNumIO) return NULL ;
	CBonFSHybrid *tuner = static_cast<CBonFSHybrid*>(arg) ;
	CAsyncFifo::CACHE *cache = tuner->m_fifo->BeginWriteBack(TSALLOCWAITING) ;
	if(!cache) return NULL ;
	else cache->resize(max_size) ;
	tuner->m_coPurge.lock() ;
	tuner->m_mapCache[id] = cache ;
	tuner->m_coPurge.unlock() ;
	return cache->data() ;
}
//---------------------------------------------------------------------------
void CBonFSHybrid::OnWriteBackFinish(int id, size_t wrote_size, void *arg)
{
	CBonFSHybrid *tuner = static_cast<CBonFSHybrid*>(arg) ;
	exclusive_lock purgeLock(&tuner->m_coPurge);
	CAsyncFifo::CACHE *cache = tuner->m_mapCache[id] ;
	if(cache) {
		tuner->m_mapCache[id]=NULL ;
		purgeLock.unlock();
		if(TSCACHING_DEFRAGMENT) {
			if ( tuner->m_fifo->Push(cache->data(),
				 static_cast<DWORD>(wrote_size), false, TSALLOCWAITING) > 0)
				tuner->m_eoCaching.set();
			cache->resize(0);
			tuner->m_fifo->FinishWriteBack(cache,!wrote_size?true:false) ;
		}else {
			cache->resize(wrote_size) ;
			if(tuner->m_fifo->FinishWriteBack(cache))
				tuner->m_eoCaching.set() ;
		}
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::OnWriteBackPurge(void *arg)
{
	CBonFSHybrid *tuner = static_cast<CBonFSHybrid*>(arg) ;
	exclusive_lock purgeLock(&tuner->m_coPurge);
	ZeroMemory(tuner->m_mapCache,sizeof(tuner->m_mapCache));
	tuner->m_fifo->Purge(true) ;
}
//---------------------------------------------------------------------------
// CBonFSHybrid::CHANNEL
//-----
CBonFSHybrid::CHANNEL::CHANNEL()
 : Space(L""),Band(BAND_na),Name(L""),Freq(0),Stream(0),TSID(0) {}
//-----
CBonFSHybrid::CHANNEL::CHANNEL(wstring space, BAND band, int channel,
  wstring name, unsigned stream, unsigned tsid) {
	Space = space ;
	Band = band ;
	Name = name ;
	Freq = FreqFromBandCh(band,channel) ;
	Stream = stream ;
	TSID = tsid ;
}
//-----
CBonFSHybrid::CHANNEL::CHANNEL(wstring space, DWORD freq, wstring name,
  unsigned stream, unsigned tsid){
	Space = space ;
	Band = BandFromFreq(freq) ;
	Name = name ;
	Freq = freq ;
	Stream = stream ;
	TSID = tsid ;
}
//-----
CBonFSHybrid::CHANNEL::CHANNEL(const CHANNEL &src) {
	Space = src.Space ;
	Band = src.Band ;
	Name = src.Name ;
	Freq = src.Freq ;
	Stream = src.Stream ;
	TSID = src.TSID;
}
//-----
DWORD CBonFSHybrid::CHANNEL::FreqFromBandCh(BAND band,int ch) {
	DWORD freq =0 ;
	switch(band) {
		case BAND_VU:
			if(ch < 4)          freq =  93UL + (ch - 1)   * 6UL ;
			else if(ch < 8)     freq = 173UL + (ch - 4)   * 6UL ;
			else if(ch < 13)    freq = 195UL + (ch - 8)   * 6UL ;
			else if(ch < 63)    freq = 473UL + (ch - 13)  * 6UL ;
			else if(ch < 122)   freq = 111UL + (ch - 113) * 6UL ;
			else if(ch ==122)   freq = 167UL ; // C22
			else if(ch < 136)   freq = 225UL + (ch - 123) * 6UL ;
			else                freq = 303UL + (ch - 136) * 6UL ;
			freq *= 1000UL ; // kHz
			freq +=  143UL ; // + 1000/7 kHz
			break ;
		case BAND_BS:
			freq = ch * 19180UL + 1030300UL ;
			break ;
		case BAND_ND:
			freq = ch * 20000UL + 1573000UL ;
			break ;
	}
	return freq ;
}
//-----
CBonFSHybrid::BAND CBonFSHybrid::CHANNEL::BandFromFreq(DWORD freq) {
	if(freq < 60000UL || freq > 2456123UL )
		return BAND_na ;
	if(freq < 900000UL )
		return BAND_VU ;
	if(freq < 1573000UL )
		return BAND_BS ;
	return BAND_ND ;
}
//===========================================================================
