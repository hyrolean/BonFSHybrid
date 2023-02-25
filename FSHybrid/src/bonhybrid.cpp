//===========================================================================
#include "stdafx.h"
//---------------------------------------------------------------------------

#include <string>
#include <iterator>
#include <climits>
#include "bonhybrid.h"
#include "usbdevfile.h"

//---------------------------------------------------------------------------

using namespace std;

//===========================================================================
namespace BonHybrid {
//---------------------------------------------------------------------------

#define UNITEDINIFILENAME "BonDriver_FSHybrid.ini"
#define INIVALUELOADER_SECTION "BonTuner"
#define INICHANNELS_SECTION "Channels"

extern "C" {

// 有効にするとtrinity19683さん直伝のキャッシュ方式に変更
BOOL TSCACHING_LEGACY = FALSE ;
// キャッシュを整合化するかどうか（安定するが多少負荷がかかる）
BOOL TSCACHING_DEFRAGMENT = FALSE ;
// キャッシュを整合化する場合のパケットサイズ
DWORD TSCACHING_DEFRAGSIZE = 128*1024 ; // 128K(Spinelに最適)
// バルク転送モード時のオーバーライドパケットサイズ
int TSCACHING_BULKPACKETSIZE = 0 ; // 1以上で有効
// NULLパケットをシャットアウトするかどうか[1:する / 0:しない]
int TSCACHING_DROPNULLPACKETS = 1;

// 非同期キャッシュの設定
DWORD ASYNCTS_QUEUENUM     = 66  ; // Default 3M (47K*66) bytes
DWORD ASYNCTS_QUEUEMAX     = 660 ; // Maximum growth 30M (47K*660) bytes
DWORD ASYNCTS_EMPTYBORDER  = 22  ; // Empty border at least 1M (47K*22) bytes
DWORD ASYNCTS_EMPTYLIMIT   = 11  ; // Empty limit at least 0.5M (47K*11) bytes
DWORD TSALLOC_TIMEOUT      = 100 ;
BOOL  TSALLOC_WAITING      = FALSE ;
BOOL  TSALLOC_MODERATE     = TRUE ;
int   TSALLOC_PRIORITY     = THREAD_PRIORITY_HIGHEST ;

// 既定のチャンネル情報にVHFを含めるかどうか
BOOL DEFSPACE_VHF = FALSE ;
// 既定のチャンネル情報にUHFを含めるかどうか
BOOL DEFSPACE_UHF = TRUE ;
// 既定のチャンネル情報にCATVを含めるかどうか
BOOL DEFSPACE_CATV = FALSE ;
// 既定のチャンネル情報にレジストリのチャンネル情報を含めるかどうか
BOOL DEFSPACE_AUX = TRUE ;
// 既定の三波チューナーチャンネル情報にBSを含めるかどうか
BOOL DEFSPACE_BS = TRUE ;
// 既定の三波チューナーBSチャンネルの各ストリーム数(0-8)
DWORD DEFSPACE_BS_STREAMS = 8 ;
// 既定の三波チューナーBSチャンネルをストリーム基準に配置するかどうか
BOOL DEFSPACE_BS_STREAM_STRIDE = FALSE ;
// 既定の三波チューナーチャンネル情報にCS110を含めるかどうか
BOOL DEFSPACE_CS110 = TRUE ;
// 既定の三波チューナーCS110チャンネルの各ストリーム数(0-8)
DWORD DEFSPACE_CS110_STREAMS = 8 ;
// 既定の三波チューナーCS110チャンネルをストリーム基準に配置するかどうか
BOOL DEFSPACE_CS110_STREAM_STRIDE = FALSE ;
// IBonDriverのSetChannelにユーザーチャンネルを使用するかどうか
BOOL BYTETUNING_USER = FALSE ;

// 初期化に失敗した場合の再試行設定
DWORD DEVICE_RETRY_TIMES = 3 ;       // デバイス初期化再試行最大回数
DWORD TUNER_RETRY_DURATION = 3000 ;  // チューナーのオープン最大再試行時間

// チューナー自動参照時に機器の参照IDを循環させるようにするかどうか
BOOL DEVICE_ID_ROTATION = FALSE ;          // IDを循環参照させるかどうか
BOOL DEVICE_ID_ROTATION_VOLATILE = FALSE ; // 揮発性レジストリに書くかどうか

}

// Instance
CBonFSHybrid* CBonFSHybrid::m_pThis = NULL;
HINSTANCE CBonFSHybrid::m_hModule = NULL;


//#define ES_PREVIEW

// エンジニアリングサンプル用の時限設定コード
#ifdef ES_PREVIEW
#define ES_LIMIT (60*60*1000) // 1時間経過したらストリームを強制停止する
static DWORD ES_Past = 0 ;
static bool ES_Elapsed() {
	if(!ES_Past) ES_Past = Elapsed() ;
	return Elapsed(ES_Past)>ES_LIMIT ;
}
#endif


//===========================================================================
// Value Loaders
//---------------------------------------------------------------------------
class CRegValueLoader : public IValueLoader
{
	HKEY HKey ;
public:
	CRegValueLoader(HKEY hKey): HKey(hKey) {}
	virtual DWORD ReadDWORD(const wstring name,DWORD defVal=0) const {
		BYTE buf[sizeof DWORD] ;
		DWORD rdSize = sizeof DWORD ;
		DWORD type = 0 ;
		if(ERROR_SUCCESS==RegQueryValueEx(
		  HKey, name.c_str(), 0, &type, buf, &rdSize )) {
			if(type==REG_DWORD) {
				DWORD result = *(DWORD*)(&buf[0]) ;
				DBGOUT("Mode: %s=%d\n",wcs2mbcs(name).c_str(),result);
				return result ;
			}
		}
		return defVal ;
	}
	virtual wstring ReadString(const wstring name,const wstring defStr) const {
		const size_t MAX_BYTES = 1024 ;
		BYTE buf[MAX_BYTES] ;
		DWORD rdSize = sizeof buf ;
		DWORD type = 0 ;
		if(ERROR_SUCCESS==RegQueryValueEx(
		  HKey, name.c_str(), 0, &type, buf, &rdSize )) {
			if(type==REG_SZ) {
				wstring result((LPCTSTR)(&buf[0])) ;
				DBGOUT("Mode: %s=%s\n",wcs2mbcs(name).c_str(),wcs2mbcs(result).c_str());
				return result ;
			}
		}
		return defStr ;
	}
};
//---------------------------------------------------------------------------
class CIniValueLoader : public IValueLoader
{
	string Section, Filename ;
	string ReadStringA(const string name,const string defStr) const {
		const size_t MAX_CHARS = 1024 ;
		char buf[MAX_CHARS] ;
		DWORD num = GetPrivateProfileStringA(
			Section.c_str(),name.c_str(),defStr.c_str(),
			buf,MAX_CHARS,Filename.c_str());
		return num>0 ? string(buf,num) : defStr ;
	}
public:
	CIniValueLoader(const string section,const string filename)
	{ Section=section; Filename=filename; }
	virtual DWORD ReadDWORD(const wstring name,DWORD defVal=0) const {
	#if 0
		return (DWORD) GetPrivateProfileIntA(
			Section.c_str(),wcs2mbcs(name).c_str(),(int)defVal,Filename.c_str()) ;
	#else
		return (DWORD) acalci(ReadStringA(wcs2mbcs(name),"").c_str(),(int)(defVal));
	#endif
	}
	virtual wstring ReadString(const wstring name,const wstring defStr) const {
		return mbcs2wcs(ReadStringA(wcs2mbcs(name),wcs2mbcs(defStr)));
	}
};
//===========================================================================
// CBonFSHybrid
//---------------------------------------------------------------------------
CBonFSHybrid::CBonFSHybrid(bool hasSatellite)
{
	m_pThis = this;
	m_hasSatellite=hasSatellite ;
	tsthr=NULL ;
	m_fifo=NULL ;
	FifoFinalize() ;
}
//---------------------------------------------------------------------------
CBonFSHybrid::~CBonFSHybrid()
{
	FifoFinalize() ;
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
	bool result = false, breath = true ;
	DWORD counter = 0;
	hDev=hUsbDev=NULL;
	int user_idx = UserDecidedDeviceIdx();
	int rot_idx = -1 ;
	wstring rot_nam = L"ROTATION_DEVICE_ID("+wstring(GetTunerName())+L")" ;
	wstring reg_nam = RegName() ;
	if(DEVICE_ID_ROTATION_VOLATILE) reg_nam += L"\\__volatile__" ;
	if(DEVICE_ID_ROTATION && user_idx<0) {
		HKEY hKey ;
		if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, reg_nam.c_str(), 0, KEY_READ, &hKey)) {
			rot_idx = (int) CRegValueLoader(hKey).ReadDWORD(rot_nam,0) + 1 ;
			RegCloseKey(hKey);
		}else rot_idx=0;
	}
	do {
		int idx = user_idx < 0 ? rot_idx : user_idx ;
		if(hDev||hUsbDev) {
			Sleep(50);
			FreeDevice(hDev,hUsbDev);
		}
		if(counter>0) Sleep(1000) ; //# take a breath before retrying...
		if((hDev = usbdevfile_alloc(&idx,&drvGUID) ) == NULL) {
			if(user_idx<0&&rot_idx>=0) {
				if(rot_idx!=0) rot_idx^=rot_idx;
				else rot_idx=-1;
				counter--;
			}
			continue; //# not found
		}
		if((hUsbDev = usbdevfile_init(hDev) ) == NULL) {
			continue; //# failed
		}
		result = true ;
		rot_idx = idx ;
	}while(!result&&++counter<=DEVICE_RETRY_TIMES);
	if(result && DEVICE_ID_ROTATION && rot_idx>=0) {
		HKEY hKey ; DWORD dwDisposition ;
		if( ERROR_SUCCESS == RegCreateKeyEx(
				HKEY_CURRENT_USER, reg_nam.c_str(), 0, NULL,
				DEVICE_ID_ROTATION_VOLATILE?
					REG_OPTION_VOLATILE: REG_OPTION_NON_VOLATILE,
				KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition) ) {
			RegSetValueEx( hKey, rot_nam.c_str(), 0, REG_DWORD,
				(BYTE*)&rot_idx, sizeof DWORD );
			RegCloseKey(hKey);
		}
	}
	return result ;
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
void CBonFSHybrid::LoadUserChannels()
{
	string chFName = file_path_of(ModuleFileName()) + file_prefix_of(ModuleFileName()) + ".ch.txt" ;

	FILE *st = NULL ;
	fopen_s(&st, chFName.c_str(), "rt") ;
	if (!st)
		return ;
	char s[512] ;

	CHANNELS channels ;
	SPACEINDICES indices ;

	std::wstring space_name = L"" ;
	while (!feof(st)) {
		s[0] = '\0' ;
		fgets(s, 512, st) ;
		string strLine = trim(string(s)) ;
		if (strLine.empty())
			continue ;
		wstring wstrLine = mbcs2wcs(strLine) ;
		int t = 0 ;
		vector<wstring> params ;
		split(params, wstrLine, L';') ;
		wstrLine = params[0] ;
		params.clear() ;
		split(params, wstrLine, L',') ;
		if (params.size() >= 2 && !params[0].empty()) {
			BAND band = BAND_na ;
			int channel = 0 ;
			DWORD freq = 0 ;
			int stream = 0 ;
			int tsid = 0 ;
			wstring &space = params[0] ;
			wstring name = params.size() >= 3 ? params[2] : wstring(L"") ;
			wstring subname = params[1] ;
			vector<wstring> phyChDiv ;
			split(phyChDiv, params[1], '/') ;
			for (size_t i = 0;i < phyChDiv.size();i++) {
				wstring phyCh = phyChDiv[i] ;
				if ( phyCh.length() > 3 &&
						phyCh.substr(phyCh.length() - 3) == L"MHz" ) {
					float megaHz = 0.f ;
					if (swscanf_s(phyCh.c_str(), L"%fMHz", &megaHz) == 1) {
						freq = DWORD(megaHz * 1000.f) ;
						channel = CHANNEL::BandFromFreq(freq) != BAND_na ? -1 : 0 ;
					}
				} else {
					if (m_hasSatellite && swscanf_s(phyCh.c_str(), L"TS%d", &stream) == 1)
						;
					else if (m_hasSatellite && swscanf_s(phyCh.c_str(), L"ID%i", &tsid) == 1)
						;
					else if (band == BAND_na) {
						if (m_hasSatellite && swscanf_s(phyCh.c_str(), L"BS%d", &channel) == 1)
							band = BAND_BS ;
						else if (m_hasSatellite && swscanf_s(phyCh.c_str(), L"ND%d", &channel) == 1)
							band = BAND_ND ;
						else if (swscanf_s(phyCh.c_str(), L"C%d", &channel) == 1)
							band = BAND_VU, subname = L"C" + itows(channel) + L"ch", channel += 100 ;
						else if (swscanf_s(phyCh.c_str(), L"%d", &channel) == 1)
							band = BAND_VU, subname = itows(channel) + L"ch" ;
					}
				}
			}
			if (name == L"")
				name = subname ;
			if (freq > 0 && channel < 0)
				channels.push_back(
					CHANNEL(space, freq, name, stream, tsid)) ;
			else if (band != BAND_na && channel > 0)
				channels.push_back(
					CHANNEL(space, band, channel, name, stream, tsid)) ;
			else
				continue ;
			if (space_name != space) {
				indices.push_back(channels.size() - 1) ;
				space_name = space ;
			}
		}
	}

	if (!channels.empty() && !indices.empty()) {
		m_UserChannels.swap(channels) ;
		m_UserSpaceIndices.swap(indices) ;
	}

	fclose(st) ;
}
//---------------------------------------------------------------------------
void CBonFSHybrid::BuildTChannels()
{
	if(DEFSPACE_VHF) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		for(int i=1;i<=12;i++) {
			m_UserChannels.push_back(CHANNEL(L"VHF",BAND_VU,i,itows(i)+L"ch"));
		}
	}
	if(DEFSPACE_UHF) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		for(int i=13;i<=62;i++) {
			m_UserChannels.push_back(CHANNEL(L"UHF",BAND_VU,i,itows(i)+L"ch"));
		}
	}
	if(DEFSPACE_CATV) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		for(int i=13;i<=63;i++) {
			m_UserChannels.push_back(CHANNEL(L"CATV",BAND_VU,i+100,L"C"+itows(i)+L"ch"));
		}
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::BuildSChannels()
{
	if(DEFSPACE_BS) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		if(DEFSPACE_BS_STREAMS) {
			if(DEFSPACE_BS_STREAM_STRIDE) {
				for(DWORD j=0;j<DEFSPACE_BS_STREAMS;j++)
				for(int i=1;i<=23;i+=2)
					m_UserChannels.push_back(CHANNEL(L"BS",BAND_BS,i,L"BS"+itows(i)+L"/TS"+itows(j),j));
			}else {
				for(int i=1;i<=23;i+=2)
				for(DWORD j=0;j<DEFSPACE_BS_STREAMS;j++)
					m_UserChannels.push_back(CHANNEL(L"BS",BAND_BS,i,L"BS"+itows(i)+L"/TS"+itows(j),j));
			}
		}else {
			for(int i=1;i<=23;i+=2)
				m_UserChannels.push_back(CHANNEL(L"BS",BAND_BS,i,L"BS"+itows(i),0));
		}
	}
	if(DEFSPACE_CS110) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		if(DEFSPACE_CS110_STREAMS) {
			if(DEFSPACE_CS110_STREAM_STRIDE) {
				for(DWORD j=0;j<DEFSPACE_CS110_STREAMS;j++)
				for(int i=2;i<=24;i+=2)
					m_UserChannels.push_back(CHANNEL(L"CS110",BAND_ND,i,L"ND"+itows(i)+L"/TS"+itows(j),j));
			}else {
				for(int i=2;i<=24;i+=2)
				for(DWORD j=0;j<DEFSPACE_CS110_STREAMS;j++)
					m_UserChannels.push_back(CHANNEL(L"CS110",BAND_ND,i,L"ND"+itows(i)+L"/TS"+itows(j),j));
			}
		}else {
			for(int i=2;i<=24;i+=2)
				m_UserChannels.push_back(CHANNEL(L"CS110",BAND_ND,i,L"ND"+itows(i),0));
		}
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::BuildAuxChannels()
{
	if(!DEFSPACE_AUX) return ;
	CHANNELS channels ;

	string path = file_path_of(ModuleFileName()) ;
	string dllIniFileName = path + file_prefix_of(ModuleFileName()) + ".ini" ;
	ReadIniChannels(dllIniFileName,channels);
	if(channels.empty()) {
		string unitedIniFileName = path + UNITEDINIFILENAME ;
		ReadIniChannels(unitedIniFileName,channels);
		if(channels.empty()) {
			HKEY hKey;
			if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, RegName(), 0, KEY_READ, &hKey)) {
				ReadRegChannels(hKey,channels);
				RegCloseKey(hKey);
			}
			if(channels.empty()) {
				if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, RegName(), 0, KEY_READ, &hKey)) {
					ReadRegChannels(hKey,channels);
					RegCloseKey(hKey);
				}
			}
		}
	}
	if(!channels.empty()) {
		m_UserSpaceIndices.push_back(m_UserChannels.size()) ;
		copy(channels.begin(),channels.end(),back_inserter(m_UserChannels)) ;
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::ArrangeChannels()
{
	struct space_finder : public std::unary_function<CHANNEL, bool> {
		std::wstring space ;
		space_finder(std::wstring space_) {
			space = space_ ;
		}
		bool operator ()(const CHANNEL &ch) const {
			return space == ch.Space;
		}
	};
	if (!m_InvisibleSpaces.empty() || !m_SpaceArrangement.empty()) {
		CHANNELS newChannels ;
		//CHANNELS oldChannels(m_UserChannels) ;
		CHANNELS &oldChannels = m_UserChannels ;
		CHANNELS::iterator beg = oldChannels.begin() ;
		CHANNELS::iterator end = oldChannels.end() ;
		for (CHANNELS::size_type i = 0; i < m_InvisibleSpaces.size(); i++) {
			end = remove_if(beg, end, space_finder(m_InvisibleSpaces[i]));
		}
		for (CHANNELS::size_type i = 0; i < m_SpaceArrangement.size(); i++) {
			space_finder finder(m_SpaceArrangement[i]) ;
			remove_copy_if(beg, end, back_inserter(newChannels), not1(finder)) ;
			end = remove_if(beg, end, finder) ;
		}
		copy(beg, end, back_inserter(newChannels)) ;
		SPACEINDICES newSpaceIndices ;
		wstring space = L"" ;
		for (CHANNELS::size_type i = 0;i < newChannels.size();i++) {
			if (newChannels[i].Space != space) {
				space = newChannels[i].Space ;
				newSpaceIndices.push_back(size_t(i)) ;
			}
		}
		m_UserChannels.swap(newChannels) ;
		m_UserSpaceIndices.swap(newSpaceIndices) ;
	}
}
//---------------------------------------------------------------------------
CBonFSHybrid::CHANNEL *CBonFSHybrid::GetUserChannel(DWORD dwSpace, DWORD dwChannel)
{
	if(dwSpace<m_UserSpaceIndices.size()) {
		DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
		DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size()
			? m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
		if(dwChannel<end-begin) {
			DWORD index = begin + dwChannel ;
			return &m_UserChannels[index] ;
		}
	}
	return NULL ;
}
//---------------------------------------------------------------------------
CBonFSHybrid::CHANNEL CBonFSHybrid::GetChannel(DWORD dwSpace, DWORD dwChannel)
{
	if(dwSpace == SPACE_CHASFREQ) {  //# dwChannel as freq/kHz
		return CHANNEL(L"CHASFREQ",dwChannel,itows(dwChannel)+L"kHz") ;
	}else if(CHANNEL *userChannel = GetUserChannel(dwSpace,dwChannel)) {
		return *userChannel ;
	}
	return CHANNEL() ;
}
//---------------------------------------------------------------------------
bool CBonFSHybrid::FifoInitialize(usb_endpoint_st *usbep)
{
	tsfifo_t *ptsfifo = NULL ;
	tsfifo_t tsfifo={0} ;
	FifoFinalize();
	if(!TSCACHING_LEGACY) {
		if(m_eoCaching.is_valid()) {
			size_t TSDATASIZE = ROUNDUP(usbep->xfer_size,0x1FFUL) ;
			if(usbep->endpoint&0x100) {  // Isochronous
				// アイソクロナス転送の場合は、強制的に整合化する
				if(!TSCACHING_DEFRAGMENT) {
					//フレームからパケットにサイズを拡張
					TSCACHING_DEFRAGSIZE = TSCACHING_BULKPACKETSIZE>0 ?
						TSCACHING_BULKPACKETSIZE : TS_PacketSize ;
					TSCACHING_DEFRAGMENT = TRUE ; //強制
				}
			}
			if(TSCACHING_DEFRAGMENT)
				TSDATASIZE = max(TSCACHING_DEFRAGSIZE,TSDATASIZE);
			DBGOUT("TSDATASIZE=%d\n",TSDATASIZE) ;
			m_fifo = new CAsyncFifo(
				ASYNCTS_QUEUENUM,ASYNCTS_QUEUEMAX,ASYNCTS_EMPTYBORDER,
				TSDATASIZE,TSALLOC_TIMEOUT,TSALLOC_PRIORITY ) ;
			if(m_fifo) {
				m_fifo->SetEmptyLimit(ASYNCTS_EMPTYLIMIT) ;
				m_fifo->SetModerateAllocating(TSALLOC_MODERATE?true:false);
				if(TSCACHING_DEFRAGMENT) {
					tsfifo.writeThrough = &OnTSFifoWriteThrough ;
				}else {
					tsfifo.writeBackBegin = &OnTSFifoWriteBackBegin ;
					tsfifo.writeBackFinish = &OnTSFifoWriteBackFinish ;
				}
				tsfifo.purge = &OnTSFifoPurge ;
				tsfifo.arg = this ;
				ptsfifo = &tsfifo ;
			}
		}
	}
	//# TS receive thread
	if( tsthread_create(&tsthr, usbep, ptsfifo) ) {
		return false;
	}
	return true;
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
void CBonFSHybrid::ReadRegChannels(HKEY hPKey, CHANNELS &regChannels)
{
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
	regChannels.resize(NumOfValues) ;
	for(DWORD dwIdx = 0; dwIdx < NumOfValues; dwIdx++ ) {
		dwLen = 32;
		dwByte = sizeof(dwValue);
		if(ERROR_SUCCESS != RegEnumValue( hKey, dwIdx, szValueName, &dwLen, NULL, &dwType, (BYTE*)&dwValue, &dwByte)
			|| dwByte != sizeof(DWORD)) {
			break;
		}
		dwByte = dwValue >> 24; //# Index
		if( dwByte >= NumOfValues ) continue;
		regChannels[dwByte] = CHANNEL(L"AUX",dwValue & 0x00ffffff,szValueName) ;
	}
	RegCloseKey(hKey);
}
//---------------------------------------------------------------------------
void CBonFSHybrid::ReadIniChannels (const std::string iniFilename, CHANNELS &iniChannels)
{
	BUFFER<char> buf(256) ;
	DWORD n = 0;
	do {
		if (n) buf.resize(buf.size()*2) ;
		n = GetPrivateProfileSectionA(INICHANNELS_SECTION, buf.data(), (DWORD)buf.size(), iniFilename.c_str());
	} while (n == buf.size() - 2);
	if (!n)
		return ;
	size_t numChannel = 0 ;
	for (size_t i = 0;i < n;i++) {
		char *p = buf.data() ;
		p = &p[i] ;
		if (!*p)
			break ;
		numChannel++ ;
		i += strlen(p);
	}
	if (!numChannel)
		return ;
	iniChannels.resize(numChannel) ;
	for (size_t i = 0;i < n;i++) {
		char *p = buf.data() ;
		p = &p[i] ;
		if (!*p)
			break ;
		vector<string> item ;
		split(item, string(p), '=') ;
		if (item.size() == 2) {
			int val = acalci(item[1].c_str(),-1) ;
			if (val!=-1) {
				size_t idx = size_t(val) >> 24;
				if(idx < numChannel)
					iniChannels[idx] = CHANNEL(L"AUX", val & 0x00ffffff, mbcs2wcs(item[0])) ;
			}
		}
		i += strlen(p);
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::LoadReg()
{
	HKEY hKey;
	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, RegName(), 0, KEY_READ, &hKey)) {
		CRegValueLoader Loader(hKey) ;
		LoadValues(&Loader);
		RegCloseKey(hKey);
	}
	if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, RegName(), 0, KEY_READ, &hKey)) {
		CRegValueLoader Loader(hKey) ;
		LoadValues(&Loader);
		RegCloseKey(hKey);
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::LoadIni()
{
	string path = file_path_of(ModuleFileName()) ;
	string unitedIniFileName = path + UNITEDINIFILENAME ;
	CIniValueLoader unitedIniLoader(INIVALUELOADER_SECTION, unitedIniFileName) ;
	LoadValues(&unitedIniLoader);
	string dllIniFileName = path + file_prefix_of(ModuleFileName()) + ".ini" ;
	CIniValueLoader dllIniLoader(INIVALUELOADER_SECTION, dllIniFileName) ;
	LoadValues(&dllIniLoader);
}
//---------------------------------------------------------------------------
void CBonFSHybrid::LoadValues(const IValueLoader *Loader)
{
	#define LOADDW(val) do { val = Loader->ReadDWORD(L#val,val); } while(0)
	#define LOADMSTRLIST(name) do { \
		wstring s = Loader->ReadString(L#name); \
		if(!s.empty()) { \
			m_##name.clear(); split(m_##name,s,','); \
		} }while(0)
	LOADDW(TSCACHING_LEGACY);
	LOADDW(TSCACHING_DEFRAGMENT);
	LOADDW(TSCACHING_DEFRAGSIZE);
	LOADDW(TSCACHING_BULKPACKETSIZE);
	LOADDW(TSCACHING_DROPNULLPACKETS);
	LOADDW(ASYNCTS_QUEUENUM);
	LOADDW(ASYNCTS_QUEUEMAX);
	LOADDW(ASYNCTS_EMPTYBORDER);
	LOADDW(ASYNCTS_EMPTYLIMIT);
	LOADDW(TSALLOC_TIMEOUT);
	LOADDW(TSALLOC_WAITING);
	LOADDW(TSALLOC_MODERATE);
	LOADDW(TSALLOC_PRIORITY);
	LOADDW(DEFSPACE_VHF);
	LOADDW(DEFSPACE_UHF);
	LOADDW(DEFSPACE_CATV);
	LOADDW(DEFSPACE_AUX);
	LOADDW(DEFSPACE_BS);
	LOADDW(DEFSPACE_BS_STREAMS);
	LOADDW(DEFSPACE_BS_STREAM_STRIDE);
	LOADDW(DEFSPACE_CS110);
	LOADDW(DEFSPACE_CS110_STREAMS);
	LOADDW(DEFSPACE_CS110_STREAM_STRIDE);
	LOADDW(BYTETUNING_USER);
	LOADDW(DEVICE_RETRY_TIMES);
	LOADDW(TUNER_RETRY_DURATION);
	LOADDW(DEVICE_ID_ROTATION);
    LOADDW(DEVICE_ID_ROTATION_VOLATILE);
	LOADMSTRLIST(SpaceArrangement);
	LOADMSTRLIST(InvisibleSpaces);
	LOADDW(TSTHREAD_DUPLEX);
	LOADDW(TSTHREAD_PRIORITY);
	LOADDW(TSTHREAD_POLL_TIMEOUT);
	LOADDW(TSTHREAD_SUBMIT_TIMEOUT);
	LOADDW(TSTHREAD_NUMIO);
	LOADDW(TSTHREAD_SUBMIT_IOLIMIT);
	LOADDW(TSTHREAD_LOCK_ON_WINUSB);
	LOADDW(USBPOWERPOLICY_AVOID_SUSPEND);
	LOADDW(USBPIPEPOLICY_RAW_IO);
	LOADDW(USBPIPEPOLICY_AUTO_CLEAR_STALL);
	LOADDW(USBPIPEPOLICY_ALLOW_PARTIAL_READS);
	LOADDW(USBPIPEPOLICY_AUTO_FLUSH);
	LOADDW(USBPIPEPOLICY_IGNORE_SHORT_PACKETS);
	LOADDW(USBPIPEPOLICY_SHORT_PACKET_TERMINATE);
	LOADDW(USBPIPEPOLICY_PIPE_TRANSFER_TIMEOUT);
	LOADDW(USBPIPEPOLICY_RESET_PIPE_ON_RESUME);
	#undef LOADMSTRLIST
	#undef LOADDW
}
//---------------------------------------------------------------------------
void CBonFSHybrid::Initialize()
{
	InitConstants();
	LoadReg();
	LoadIni();
	BuildTChannels();
	BuildAuxChannels();
	if(m_hasSatellite)
		BuildSChannels();
	LoadUserChannels();
	ArrangeChannels();
}
//---------------------------------------------------------------------------
void CBonFSHybrid::InitConstants()
{
#define ACALCI_ENTRY_CONST(name) do { \
		acalci_entry_const(#name,(int)name); \
		acalci64_entry_const(#name,(__int64)name); \
		}while(0)
	const int TS_LegacyPacketSize =  188*245 ;
	ACALCI_ENTRY_CONST(TS_MaxNumIO);
	//ACALCI_ENTRY_CONST(TS_BufPackets);
	ACALCI_ENTRY_CONST(TS_PacketSize);
	ACALCI_ENTRY_CONST(TS_LegacyPacketSize);
	#ifdef INCLUDE_ISOCH_XFER
	ACALCI_ENTRY_CONST(ISOCH_FrameSize);
	ACALCI_ENTRY_CONST(ISOCH_PacketFrames);
	#endif
#undef ACALCI_ENTRY_CONST
}
//---------------------------------------------------------------------------
const BOOL CBonFSHybrid::OpenTuner()
{
	if(IsTunerOpening()) return FALSE;
	BOOL r; DWORD e=Elapsed();
	do { r=TryOpenTuner(); } while (!r && Elapsed(e)<TUNER_RETRY_DURATION);
	return r ;
}
//---------------------------------------------------------------------------
const BOOL CBonFSHybrid::SetChannel(const BYTE bCh)
{
	if(BYTETUNING_USER) {
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
		if( (DEFSPACE_VHF&&bCh>=1&&bCh<=12) ||
			(DEFSPACE_UHF&&bCh>=13&&bCh<=62) ||
			(DEFSPACE_CATV&&bCh>=113&&bCh<=163) ) {
			return SetChannel(SPACE_CHASFREQ, CHANNEL::FreqFromBandCh(BAND_VU,bCh));
		}
	}
	return FALSE ;
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
#ifdef ES_PREVIEW
	if(ES_Elapsed()) { if(m_fifo) m_fifo->Purge(false) ; return FALSE ; }
#endif
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
#ifdef ES_PREVIEW
	if(ES_Elapsed()) { if(m_fifo) m_fifo->Purge(false) ; return FALSE ; }
#endif
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
	if(dwSpace<m_UserSpaceIndices.size())
		return m_UserChannels[m_UserSpaceIndices[dwSpace]].Space.c_str() ;
	return NULL ;
}
//---------------------------------------------------------------------------
LPCTSTR CBonFSHybrid::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	static TCHAR buf[8];
	if(dwSpace<m_UserSpaceIndices.size()) {
		DWORD begin = (DWORD)m_UserSpaceIndices[dwSpace] ;
		DWORD end = DWORD(dwSpace+1 < m_UserSpaceIndices.size() ?
			m_UserSpaceIndices[dwSpace+1] : m_UserChannels.size()) ;
		if(dwChannel<end-begin)
			return m_UserChannels[begin+dwChannel].Name.c_str() ;
	}
	return NULL ;
}
//---------------------------------------------------------------------------
void *CBonFSHybrid::OnTSFifoWriteBackBegin(int id, size_t max_size, void *arg)
{
	if(id<0||id>=TS_MaxNumIO) return NULL ;
	CBonFSHybrid *tuner = static_cast<CBonFSHybrid*>(arg) ;
	CAsyncFifo::CACHE *cache = tuner->m_fifo->BeginWriteBack(TSALLOC_WAITING?true:false) ;
	if(!cache) return NULL ;
	else cache->resize(max_size) ;
	tuner->m_coPurge.lock() ;
	tuner->m_mapCache[id] = cache ;
	tuner->m_coPurge.unlock() ;
	return cache->data() ;
}
//---------------------------------------------------------------------------
void CBonFSHybrid::OnTSFifoWriteBackFinish(int id, size_t wrote_size, void *arg)
{
	CBonFSHybrid *tuner = static_cast<CBonFSHybrid*>(arg) ;
	exclusive_lock purgeLock(&tuner->m_coPurge);
	CAsyncFifo::CACHE *cache = tuner->m_mapCache[id] ;
	if(cache) {
		tuner->m_mapCache[id]=NULL ;
		purgeLock.unlock();
		cache->resize(wrote_size) ;
		if(tuner->m_fifo->FinishWriteBack(cache))
			tuner->m_eoCaching.set() ;
	}
}
//---------------------------------------------------------------------------
void CBonFSHybrid::OnTSFifoWriteThrough(const void *buffer, size_t size, void *arg)
{
	CBonFSHybrid *tuner = static_cast<CBonFSHybrid*>(arg) ;
	exclusive_lock purgeLock(&tuner->m_coPurge);
	if ( tuner->m_fifo->Push(static_cast<const BYTE*>(buffer),
		 static_cast<DWORD>(size), false, TSALLOC_WAITING?true:false) > 0)
			tuner->m_eoCaching.set();
}
//---------------------------------------------------------------------------
void CBonFSHybrid::OnTSFifoPurge(void *arg)
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
//---------------------------------------------------------------------------
} // End of namespace BonHybrid
//===========================================================================
