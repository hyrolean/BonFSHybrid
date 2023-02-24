
#include "stdafx.h"
#include "BonTuner_FSUSB2N.h"

using namespace std ;

namespace FSUSB2N {

// 有効にするとtri.dw.land.toさん直伝のキャッシュ方式に変更
BOOL TSCACHING_LEGACY = FALSE ;
// キャッシュを整合化するかどうか（安定するが多少負荷がかかる）
BOOL TSCACHING_DEFRAGMENT = FALSE ;
// キャッシュを整合化する場合のパケットサイズ
DWORD TSCACHING_DEFRAGSIZE = 128*1024 ; // 128K(Spinelに最適)

DWORD ASYNCTS_QUEUENUM    = 66  ; // Default 3M (47K*66) bytes
DWORD ASYNCTS_QUEUEMAX    = 660 ; // Maximum growth 30M (47K*660) bytes
DWORD ASYNCTS_EMPTYBORDER = 22  ; // Empty border at least 1M (47K*22) bytes
DWORD ASYNCTS_EMPTYLIMIT  = 11  ; // Empty limit at least 0.5M (47K*11) bytes
#define TSWRITEBACK         1
#define TSDATASIZE          USBBULK_XFERSIZE // = 64K bytes
#define TSTHREADWAIT        1000
#define TSALLOCWAITING		false
#define TSALLOCMODERATE		true
#define SUSPENDTIMEOUT      5000

// 静的メンバ初期化
CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;

const TCHAR *g_RegKey = TEXT("Software\\tri.dw.land.to\\FSUSB2N");

CBonTuner::CBonTuner()
: m_dwCurSpace(0) , m_dwCurChannel(0) , usbDev(NULL) , pDev(NULL) , m_TsBuffSize(NULL) , m_ChannelList(NULL)
, m_hThread(NULL) , m_evThreadSuspend(NULL), m_evThreadResume(NULL), m_cntThreadSuspend(0), m_fifo(NULL)
{
	m_pThis = this;
	this->LoadData();
}

CBonTuner::~CBonTuner()
{
	// 開かれてる場合は閉じる
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
	  wstring name = params.size()>=3 ? params[2] : L"" ;
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
	if(sscanf_s( upper_case(file_prefix_of(ModuleFileName())).c_str() , "BONDRIVER_FSUSB2N_DEV%d", &idx )==1)
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
	CloseTuner();

	if(IsTunerOpening())return FALSE;

	try{
		// AllocTuner
		for(int idx = UserDecidedDeviceIdx();;)
		{
			EM2874Device* pDevTmp = EM2874Device::AllocDevice(idx);
			if(pDevTmp == NULL) {
				// Deviceが見つからない場合
				throw (const DWORD)__LINE__;
			}
			// Deviceを確保した
			usbDev = pDevTmp;
			break;
		}
		// Device初期化
		usbDev->initDevice2();

		DWORD dwStart = PastSleep();
		::Sleep(80);
		if(usbDev->getDeviceID() == 2) {
			pDev = new Ktv2Device(usbDev);
		}else{
			pDev = new Ktv1Device(usbDev);
		}

		PastSleep(160,dwStart);
		pDev->InitTuner();

		PastSleep(180,dwStart);
		pDev->InitDeMod();
		pDev->ResetDeMod();

		PastSleep(500,dwStart);

		// FIFO初期化
		EM2874Device::write_back_t *pwback = NULL ;
		EM2874Device::write_back_t wback ;
		if(!TSCACHING_LEGACY) {
			m_evThreadSuspend = CreateEvent(NULL,FALSE,FALSE,NULL) ;
			m_evThreadResume = CreateEvent(NULL,FALSE,FALSE,NULL) ;
			m_cntThreadSuspend = 0 ;
			size_t PacketSize = TSDATASIZE ;
			if(TSCACHING_DEFRAGMENT)
				PacketSize = max(TSCACHING_DEFRAGSIZE,PacketSize);
			m_fifo = new CAsyncFifo(
			  ASYNCTS_QUEUENUM,ASYNCTS_QUEUEMAX,ASYNCTS_EMPTYBORDER,
			  PacketSize,TSTHREADWAIT ) ;
			if(m_fifo) {
				m_fifo->SetEmptyLimit(ASYNCTS_EMPTYLIMIT) ;
				m_fifo->SetModerateAllocating(TSALLOCMODERATE);
				#if TSWRITEBACK
				wback.begin_func = OnWriteBackBegin ;
				wback.finish_func = OnWriteBackFinish ;
				wback.arg = (void*) this ;
				pwback = &wback ;
				#endif
			}
		}

		if(!pwback) {
		  m_TsBuffSize = (int*)VirtualAlloc( NULL, RINGBUFF_SIZE*USBBULK_XFERSIZE + 0x1000, MEM_COMMIT, PAGE_READWRITE );
		  if(m_TsBuffSize == NULL)    throw (const DWORD)__LINE__;
		  m_pTsBuff = (BYTE*)(m_TsBuffSize + 0x400);
		}else {
		  m_TsBuffSize = NULL ;
		  m_pTsBuff = NULL ;
		}
		m_indexTsBuff = 0;
		m_cntThreadSuspend=0 ;

		usbDev->SetBuffer( (void*)m_TsBuffSize, pwback );
		usbDev->TransferStart();

		if(!TSCACHING_LEGACY) {
			m_hThread = (HANDLE)_beginthreadex( NULL, 0, TsThread, (PVOID)this, 0, NULL );
			if(m_hThread == INVALID_HANDLE_VALUE) {
				m_hThread = NULL;
			}else{
				::SetThreadPriority( m_hThread, THREAD_PRIORITY_HIGHEST/*TIME_CRITICAL*/ );
			}
		}

		// デバイス使用準備完了 選局はまだ
		DBGOUT("OpenTuner done.\n");
	}
	catch (
		const DWORD
#ifdef _DEBUG
		dwErrorStep
#endif
		) {
		// Error
		DBG_INFO("BonDriver_FSUSB2N:OpenTuner dwErrorStep = %lu\n", dwErrorStep);

		CloseTuner();
		return FALSE;
	}
	return TRUE;
}

void CBonTuner::CloseTuner()
{

	if(m_hThread != NULL) {
		if(m_evThreadSuspend) PulseEvent(m_evThreadSuspend) ;
		if(m_evThreadResume) PulseEvent(m_evThreadResume) ;
		usbDev->TransferStop();
		if(::WaitForSingleObject(m_hThread, 5000) != WAIT_OBJECT_0) {
			::TerminateThread(m_hThread, 0);
		}
		::CloseHandle(m_hThread);
		m_hThread = NULL;
	}

	if(m_TsBuffSize||(usbDev&&usbDev->WriteBackEnabled())) {
		usbDev->TransferStop();
		usbDev->SetBuffer(NULL);
		if(m_TsBuffSize) ::VirtualFree( (LPVOID)m_TsBuffSize, 0, MEM_RELEASE );
		m_TsBuffSize = NULL;
	}

	if(pDev) {
		delete pDev;
		pDev = NULL;
	}

	if(usbDev) {
		delete usbDev;
		usbDev = NULL;
	}

	if(m_fifo) {
	  delete m_fifo ;
	  m_fifo = NULL ;
	}
	if(m_evThreadSuspend) {
	  ::CloseHandle(m_evThreadSuspend) ;
	  m_evThreadSuspend=NULL ;
	}
	if(m_evThreadResume) {
	  ::CloseHandle(m_evThreadResume) ;
	  m_evThreadResume=NULL ;
	}

	DBGOUT("CloseTuner done.\n");
}


const BOOL CBonTuner::SetChannel(const BYTE bCh)
{
	if(UserChannelExists()) {
	  #if 0
	  for(size_t i=m_UserSpaceIndices.size()-1;i>=0;i--) {
		if(bCh>m_UserSpaceIndices[i]) {
		  return SetChannel(DWORD(i),DWORD(bCh-m_UserSpaceIndices[i])) ;
		}
	  }
	  #endif
	  return FALSE ;
	}

	// IBonDriverとの互換性を保つために暫定

	if(bCh < 13 || bCh > 52) return FALSE;
	else return SetChannel(0UL, bCh - 13U);
}

const float CBonTuner::GetSignalLevel(void)
{
	if(pDev == NULL) return 0.0f;
	return pDev->DeMod_GetQuality() * 0.01f;
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	if(GetReadyCount() > 0)
	{
		return WAIT_OBJECT_0;
	}
	if(m_TsBuffSize == NULL&&(!usbDev||!usbDev->WriteBackEnabled()))
	{
		::Sleep(dwTimeOut < 2000 ? dwTimeOut : 2000 );
		return WAIT_TIMEOUT;
	}
	if(TSCACHING_LEGACY) {
		DWORD dwRet = ::WaitForSingleObject( usbDev->GetHandle() , dwTimeOut );

		if( dwRet == WAIT_OBJECT_0  )
		{
			int nRet = usbDev->DispatchTSRead();
			if( nRet < 0 )  return WAIT_FAILED;
		}
		return dwRet;
	}else {
		return m_eoCaching.wait(dwTimeOut);
	}
}

const DWORD CBonTuner::GetReadyCount()
{
	// 取り出し可能TSデータ数を取得する(FIFO)
	if(m_fifo) return (DWORD)m_fifo->Size();

	// 取り出し可能TSデータ数を取得する(Legacy)
	if(m_TsBuffSize == NULL) return 0;
	const int indexCurrent = m_indexTsBuff;
	int val;

	DWORD dwRet = ::WaitForSingleObject( usbDev->GetHandle() , 0 );

	if( dwRet == WAIT_FAILED )
	{
		return 0;
	}else if( dwRet == WAIT_OBJECT_0 || dwRet == WAIT_TIMEOUT )
	{
		usbDev->DispatchTSRead();
	}

	// size=0をskip
	do {
		val = m_TsBuffSize[m_indexTsBuff];
		if(val > 0 || val == -1) {
			break;
		}else if(val <= -2) {
			m_indexTsBuff = 0;
		}else if(val == 0) {
			m_indexTsBuff++;
		}
	} while(m_indexTsBuff != indexCurrent);

	return m_TsBuffSize[m_indexTsBuff] < 0 ? 0 : 1;
}

const BOOL CBonTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;

	// TSデータをBufferから取り出す
	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize) {
			::CopyMemory(pDst, pSrc, *pdwSize);
		}
		return TRUE;
	}

	return FALSE;
}

const BOOL CBonTuner::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	// FIFO
	if(m_fifo) {
		m_fifo->Pop(ppDst,pdwSize,pdwRemain) ;
		return TRUE ;
	}

	// Legacy
	if(m_TsBuffSize == NULL) return FALSE;
	const unsigned int BuffBlockSize = -m_TsBuffSize[0x3ff];
	int val;

	if(GetReadyCount() == 0) {
		// 取り出し可能なデータがない
		*pdwSize = 0;
		*pdwRemain = 0;
		return TRUE;
	}

	*ppDst = m_pTsBuff + (m_indexTsBuff * BuffBlockSize);
	int dataLen = 0;
	do {
		val = m_TsBuffSize[m_indexTsBuff];

		if(val >= 0) {
			dataLen += val;
			m_indexTsBuff++;
		}
	} while(val == BuffBlockSize);
	*pdwRemain = GetReadyCount();
	*pdwSize = dataLen;

	return TRUE;
}

void CBonTuner::PurgeTsStream()
{
	// TsThreadが一時停止するのを待機する
	if(m_hThread) {
	  ThreadSuspend() ;
	}

	// バッファから取り出し可能データをパージする
	if(m_fifo) {
	  exclusive_lock lock(&m_coPurge) ;
	  ZeroMemory(m_mapCache,sizeof(m_mapCache));
	  m_fifo->Purge(true) ;
	}

	if(m_TsBuffSize&&usbDev) {
	  m_indexTsBuff=usbDev->GetTsBuffIndex() ;
	}

	// TsThreadを再開する
	if(m_hThread) {
	  ThreadResume();
	}
}

void CBonTuner::Release()
{
	// インスタンス開放
	delete this;
}

LPCTSTR CBonTuner::GetTunerName(void)
{
	// チューナ名を返す
	return TEXT("KTV-FSUSB2N");
}

const BOOL CBonTuner::IsTunerOpening(void)
{
	return pDev ? TRUE : FALSE;
}

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	if(UserChannelExists()) {
	  if(dwSpace<m_UserSpaceIndices.size())
		return m_UserChannels[m_UserSpaceIndices[dwSpace]].Space.c_str() ;
	  return NULL ;
	}
	// 使用可能なチューニング空間を返す
	if(dwSpace == 0U) return TEXT("地デジ");
	else if(m_ChannelList != NULL && dwSpace == 1U) return TEXT("地デジ(追加)");
	return NULL;
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if(UserChannelExists()) {
	  if(dwSpace<m_UserSpaceIndices.size()) {
		DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
		DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size() ? m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
		if(dwChannel<end-begin)
		  return m_UserChannels[begin+dwChannel].Name.c_str() ;
	  }
	  return NULL ;
	}

	// 使用可能なChannelを返す

	static const TCHAR ChannelNameT[][3] =
	{
		TEXT("13"), TEXT("14"), TEXT("15"), TEXT("16"), TEXT("17"), TEXT("18"), TEXT("19"),
		TEXT("20"), TEXT("21"), TEXT("22"), TEXT("23"), TEXT("24"), TEXT("25"), TEXT("26"), TEXT("27"), TEXT("28"), TEXT("29"),
		TEXT("30"), TEXT("31"), TEXT("32"), TEXT("33"), TEXT("34"), TEXT("35"), TEXT("36"), TEXT("37"), TEXT("38"), TEXT("39"),
		TEXT("40"), TEXT("41"), TEXT("42"), TEXT("43"), TEXT("44"), TEXT("45"), TEXT("46"), TEXT("47"), TEXT("48"), TEXT("49"),
		TEXT("50"), TEXT("51"), TEXT("52")
	};

	if(m_ChannelList != NULL && dwSpace == 1U) {
		/* ユーザー定義 */
		DWORD dwChannelLen      = m_ChannelList[0] >> 16;
		DWORD dwNumOfChannels   = m_ChannelList[0] & 0xffff;
		TCHAR *ptrStr = (TCHAR*)(m_ChannelList + dwNumOfChannels + 1);
		if(dwChannel < dwNumOfChannels) return ptrStr + (dwChannelLen * dwChannel);
	}else if(dwSpace == 0U) {
		if(dwChannel < 40)
			return ChannelNameT[dwChannel];
	}
	return NULL;
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if(pDev == NULL) return FALSE;
	if(NULL == EnumChannelName(dwSpace, dwChannel)) return FALSE;

	DWORD dwFreq;

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
	else if(m_ChannelList != NULL && dwSpace == 1U) {
		/* ユーザー定義 */
		dwFreq = m_ChannelList[dwChannel + 1];
	}else if(dwSpace == 0U) {
		dwFreq = dwChannel * 6000U + 473143U;
	}else{
		return FALSE;
	}
	if(dwFreq < 90000U || dwFreq > 772000U) return FALSE;

	// Channel変更
	ThreadSuspend() ;

	usbDev->TransferPause();

	for(int i=0;i<2;i++) {
		pDev->SetFrequency(dwFreq);
		::Sleep(5);
		pDev->ResetDeMod();
		::Sleep(20);
	}

	usbDev->TransferResume();
	::Sleep(10);

	PurgeTsStream();
	ThreadResume() ;



	// Channel情報更新
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;

	return TRUE;
}

const DWORD CBonTuner::GetCurSpace(void)
{
	// 現在のチューニング空間を返す
	return m_dwCurSpace;
}

const DWORD CBonTuner::GetCurChannel(void)
{
	// 現在のChannelを返す
	return m_dwCurChannel;
}

unsigned int CBonTuner::TsThreadMain ()
{
	const bool WriteBackEnabled = usbDev->WriteBackEnabled() ;
	const unsigned int BuffBlockSize = WriteBackEnabled ? 0 : -m_TsBuffSize[0x3ff];
	const bool IgnoreFragment = TSCACHING_DEFRAGMENT ? false : true ;
	DWORD dwRet;
	int nRet;

	if(!usbDev)
	  return 0;

	for(;;)
	{
		HANDLE evTs = usbDev->GetHandle() ;  if(!evTs) break ;
		dwRet = ::WaitForSingleObject( evTs , TSTHREADWAIT );

		exclusive_lock slock(&m_exSuspend) ;
		if(m_cntThreadSuspend>0) {
			  slock.unlock() ;
			  ::SetEvent(m_evThreadSuspend) ;
			  ::WaitForSingleObject(m_evThreadResume,SUSPENDTIMEOUT) ;
			  continue ;
		}
		slock.unlock() ;
		if( dwRet == WAIT_FAILED ) {
			break;
		}else if( dwRet == WAIT_OBJECT_0 /*|| dwRet == WAIT_TIMEOUT*/ ) {
		  nRet = usbDev->DispatchTSRead();
		  if(nRet < 0)    break;
		  else if(WriteBackEnabled) /* no action */ ;
		  else if(nRet > 0)  {
			if(m_TsBuffSize != NULL) {
			  const int indexCurrent = usbDev->GetTsBuffIndex() ;
			  int ln;
			  while(m_indexTsBuff != indexCurrent) {
				ln = m_TsBuffSize[m_indexTsBuff];
				if(ln>=0) {
				  // Copying received buffers to fifo.
				  BYTE *p =  m_pTsBuff + (m_indexTsBuff * BuffBlockSize) ;
				  if(m_fifo->Push(p,ln,IgnoreFragment)>0) m_eoCaching.set() ;
				  m_indexTsBuff++ ;
				}else if(ln <= -2) {
				  m_indexTsBuff = 0 ;
				}
				if(ln==-1) break ;
			  }
			}
		  }
		}
	}
	return 0;
}
unsigned int __stdcall CBonTuner::TsThread (PVOID pv)
{
	register CBonTuner *_this = static_cast<CBonTuner*>(pv) ;
	unsigned int r = _this->TsThreadMain () ;
	::_endthreadex (r);
	return r;
}
void CBonTuner::ThreadSuspend()
{
  exclusive_lock lock(&m_exSuspend) ;
  if(!m_cntThreadSuspend++) {
	  lock.unlock() ;
	  if(usbDev) {
		HANDLE evTs = usbDev->GetHandle() ;
		if(evTs) ::SetEvent(evTs) ;
	  }
	  if(m_evThreadSuspend) ::WaitForSingleObject(m_evThreadSuspend,SUSPENDTIMEOUT);
  }
}
void CBonTuner::ThreadResume()
{
  exclusive_lock lock(&m_exSuspend) ;
  if(!--m_cntThreadSuspend) {
	  lock.unlock() ;
	  if(m_evThreadResume) ::SetEvent(m_evThreadResume) ;
  }
}

bool CBonTuner::LoadData ()
{
	HKEY hKey;

	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, g_RegKey, 0, KEY_READ, &hKey))
	{
		ReadRegMode(hKey);
		ReadRegChannels(hKey);
		RegCloseKey(hKey);
	}
	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, g_RegKey, 0, KEY_READ, &hKey))
	{
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
	DWORD FunctionMode = DWORD(EM2874Device::UserSettings&0xffff) | DWORD(KtvDevice::UserSettings)<<16 ;

	#define LOADDW(val) do { val = RegReadDword(hPKey,L#val,val); } while(0)
	LOADDW(FunctionMode);
	LOADDW(TSCACHING_LEGACY);
	LOADDW(TSCACHING_DEFRAGMENT);
	LOADDW(TSCACHING_DEFRAGSIZE);
	LOADDW(ASYNCTS_QUEUENUM);
	LOADDW(ASYNCTS_QUEUEMAX);
	LOADDW(ASYNCTS_EMPTYBORDER);
	LOADDW(ASYNCTS_EMPTYLIMIT);
	#undef LOADDW

	EM2874Device::UserSettings = FunctionMode & 0xffff;
	KtvDevice::UserSettings = FunctionMode >> 16;
}

void CBonTuner::ReadRegChannels (HKEY hPKey)
{
	if(m_ChannelList != NULL) return;
	//
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
	m_ChannelList = (DWORD*) GlobalAlloc(GMEM_FIXED, NumOfValues * (dwMaxValueName * sizeof(TCHAR) + sizeof(DWORD)) + sizeof(DWORD) );
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
		dwByte = dwValue >> 24; // Index
		if( dwByte >= NumOfValues ) continue;
		m_ChannelList[dwByte + 1] = dwValue & 0x00ffffffU;
		ptrStr = (TCHAR*)(m_ChannelList + NumOfValues + 1);
		ptrStr += dwMaxValueName * dwByte;
		lstrcpyn( ptrStr, szValueName, dwMaxValueName );
	}
	RegCloseKey(hKey);
}

void *CBonTuner::OnWriteBackBegin(int id, size_t max_size, void *arg)
{
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

} // End of namespace FSUSB2N

