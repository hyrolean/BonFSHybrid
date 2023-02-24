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
#define TSALLOCWAITING		false
#define TSALLOCMODERATE		true

//# initialize static member variables
CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;

const TCHAR* const g_RegKey = TEXT("Software\\trinity19683\\FSUSB2i");

CBonTuner::CBonTuner()
: m_dwCurSpace(0), m_dwCurChannel(0), m_hDev(NULL), m_hUsbDev(NULL), pDev(NULL), tsthr(NULL),
 m_ChannelList(NULL)
{
	m_fifo=NULL ;
	ZeroMemory(m_mapCache,sizeof(m_mapCache)) ;

	m_pThis = this;
	LoadData() ;
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
	  int Channel = 0 ;
	  float MegaHz = 0.f ;
	  wstring &space = params[0] ;
	  wstring name = params.size()>=3 ? params[2] : wstring(L"") ;
      wstring subname = params[1] ;
	  if( params[1].length()>3&&
		  params[1].substr(params[1].length()-3)==L"MHz" ) {
		swscanf_s(params[1].c_str(),L"%fMHz",&MegaHz) ;
	  }else if(swscanf_s(params[1].c_str(),L"C%d",&Channel)==1) {
	    subname=L"C"+itows(Channel)+L"ch" ; Channel+=100 ;
	  }else {
		if(swscanf_s(params[1].c_str(),L"%d",&Channel)==1)
          subname=itows(Channel)+L"ch" ;
	  }
	  if(name==L"")
        name = subname ;
      if(Channel!=0||MegaHz!=0.f) {
		if(space_name!=space) {
		  m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		  space_name=space ;
		}
		m_UserChannels.push_back(
		  CHANNEL(space,Channel,name,MegaHz)) ;
	  }
	}
  }

  fclose(st) ;

}

int CBonTuner::UserDecidedDeviceIdx()
{
	int idx=0 ;
	if(sscanf_s( upper_case(file_prefix_of(ModuleFileName())).c_str() , "BONDRIVER_FSUSB2I_DEV%d", &idx )==1)
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
			if((hDev = usbdevfile_alloc(&idx,&GUID_WINUSB_FSUSB2V3_DRV) ) == NULL) {   //# not found
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
		if(it9175_create(&pDev, &m_USBEP) != 0) {
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
		if(tsthread_create(&tsthr, &m_USBEP, pwback) != 0) {
			throw (const DWORD)__LINE__;
		}

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
	if(tsthr) {
		tsthread_stop(tsthr);
		tsthread_destroy(tsthr);
		tsthr = NULL;
	}
	if(pDev) {
		it9175_destroy(pDev);
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
	if(NULL == pDev) return 0.0f;
	uint8_t statData[44];
	if(it9175_readStatistic(pDev, statData) != 0) return 0.1f;
	return statData[3] * 1.0f;
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	const int remainTime = (dwTimeOut < 0x10000000) ? dwTimeOut : 0x10000000;
	if(NULL == tsthr) return WAIT_FAILED;
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
	if(NULL == tsthr) return 0;
	if(m_fifo) return (DWORD)m_fifo->Size();
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
	  m_fifo->Pop(ppDst,pdwSize,pdwRemain) ;
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
{ return TEXT("FSUSB2i"); }

const BOOL CBonTuner::IsTunerOpening(void)
{ return m_hUsbDev ? TRUE : FALSE; }

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	if(UserChannelExists()) {
	  if(dwSpace<m_UserSpaceIndices.size())
		return m_UserChannels[m_UserSpaceIndices[dwSpace]].Space.c_str() ;
	  return NULL ;
	}
	if(dwSpace==0) return TEXT("地デジ") ;
	else if(dwSpace==1&&m_ChannelList!=NULL) return TEXT("地デジ(追加)") ;
	return NULL ;
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

	if(dwSpace == 0) {
		if(dwChannel < 40) {
			static TCHAR buf[8];
			_sntprintf_s(buf, sizeof(buf)/sizeof(TCHAR), _TRUNCATE, TEXT("%u"), dwChannel + 13);
			return buf;    //# The caller must copy data from this buffer.
		}
	}
	else if(dwSpace == 1 && m_ChannelList!=NULL) {
		//# User-defined channels
		const DWORD dwChannelLen    = m_ChannelList[0] >> 16;
		const DWORD dwNumOfChannels = m_ChannelList[0] & 0xFFFF;
		TCHAR* const ptrStr = (TCHAR*)(m_ChannelList + dwNumOfChannels + 1);
		if(dwChannel < dwNumOfChannels)	return ptrStr + (dwChannelLen * dwChannel);
	}
	return NULL;
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if(NULL == pDev) return FALSE;
	if(NULL == tsthr) return FALSE;

	DWORD dwFreq = 0;

	if(UserChannelExists()) {
	  dwFreq=0 ;
	  if(dwSpace<m_UserSpaceIndices.size()) {
		DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
		DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size() ? m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
		if(dwChannel<end-begin) {
		  DWORD index = begin + dwChannel ;
		  if(m_UserChannels[index].isMegaHzTuning()) {
			dwFreq = (DWORD) (m_UserChannels[index].MegaHz * 1000.f) ; // kHz
		  }else {
			DWORD ch = m_UserChannels[index].Channel ;
			if(ch < 4)          dwFreq =  93UL + (ch - 1)   * 6UL ;
			else if(ch < 8)     dwFreq = 173UL + (ch - 4)   * 6UL ;
			else if(ch < 13)    dwFreq = 195UL + (ch - 8)   * 6UL ;
			else if(ch < 63)    dwFreq = 473UL + (ch - 13)  * 6UL ;
			else if(ch < 122)   dwFreq = 111UL + (ch - 113) * 6UL ;
			else if(ch ==122)   dwFreq = 167UL ; // C22
			else if(ch < 136)   dwFreq = 225UL + (ch - 123) * 6UL ;
			else                dwFreq = 303UL + (ch - 136) * 6UL ;
			dwFreq *= 1000UL ; // kHz
			dwFreq +=  143UL ; // + 1000/7 kHz
		  }
		}
	  }
	}
	else if(dwSpace == 0) {//# UHF standard channels
		if(dwChannel < 40)
			dwFreq = dwChannel * 6000 + 473143;
	}else if(dwSpace == 1 && m_ChannelList != NULL) {//# User-defined channels
		const DWORD dwNumOfChannels = m_ChannelList[0] & 0xFFFF;
		if(dwChannel < dwNumOfChannels)
			dwFreq = m_ChannelList[dwChannel + 1];
	}else if(dwSpace == 114514) {  //# dwChannel as freq/kHz
		dwFreq = dwChannel;
	}
	if(dwFreq < 61000 || dwFreq > 874000 ) return FALSE;

	//# change channel
	tsthread_stop(tsthr);

	int ret;
	int cnt=0 ;
	do {
	  if(it9175_setFreq(pDev, dwFreq) != 0) {
		if(cnt++==1) return FALSE;
		continue ;
	  }
	  break ;
	}while(1) ;
	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	::Sleep( 80 );

	tsthread_start(tsthr);

	if((ret = it9175_waitTuning(pDev, 1500)) < 0) return FALSE;
	//# ignore check empty channel
	//# ignore check TS sync lock

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


} // End of namespace FSUSB2i

/*EOF*/