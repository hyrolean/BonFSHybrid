
#include "stdafx.h"
#include "BonTuner_FSUSB2N.h"

using namespace std ;

namespace FSUSB2N {

DWORD FSUSB2N_INTERIM_WAIT       = 20 ;
DWORD FSUSB2N_SETFREQ_TIMES      = 2 ;
DWORD FSUSB2N_RESETDEMOD_TIMES   = 1 ;
BOOL  FSUSB2N_LOCK_ON_SIGNAL     = TRUE ; 

const TCHAR *g_RegKey = TEXT("Software\\tri.dw.land.to\\FSUSB2N");

CBonTuner::CBonTuner()
: CBonFSHybrid(), m_dwCurSpace(0), m_dwCurChannel(0), usbDev(NULL), pDev(NULL),
  m_hDev(NULL), m_hUsbDev(NULL)
{
	ZeroMemory(&m_USBEP, sizeof m_USBEP);
}

CBonTuner::~CBonTuner()
{
	// 開かれてる場合は閉じる
	CloseTuner();
}

const TCHAR *CBonTuner::RegName()
{
	return g_RegKey ;
}

int CBonTuner::UserDecidedDeviceIdx()
{
	int idx=0 ;
	if(sscanf_s( upper_case(file_prefix_of(ModuleFileName())).c_str() , "BONDRIVER_FSUSB2N_DEV%d", &idx )==1)
	  return idx ;

	return -1 ;
}

const BOOL CBonTuner::OpenTuner()
{
	CloseTuner();

	if(IsTunerOpening())return FALSE;

	try{

		//# AllocTuner
		if(!FindDevice(GUID_WINUSB_FSUSB2N_DRV,m_hDev,m_hUsbDev)) {
		  if(m_hDev==NULL) throw (const DWORD)__LINE__;
		  if(m_hUsbDev==NULL) throw (const DWORD)__LINE__;
		}
		usbDev = EM2874Device::AllocDevice(m_hDev,m_hUsbDev);
		if(usbDev == NULL) {
			// Deviceが見つからない場合
			throw (const DWORD)__LINE__;
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

		usbDev->SetupUSBEndPoint(&m_USBEP);

		// FIFO初期化
		if(!FifoInitialize(&m_USBEP)) throw (const DWORD)__LINE__;

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

	FifoFinalize() ;

	if(usbDev) usbDev->CleanupUSBEndPoint(&m_USBEP);

	if(pDev) {
		delete pDev;
		pDev = NULL;
	}

	if(usbDev) {
		delete usbDev;
		usbDev = NULL;
	}

	FreeDevice(m_hDev,m_hUsbDev);

	DBGOUT("CloseTuner done.\n");
}

const float CBonTuner::GetSignalLevel(void)
{
	if(pDev == NULL) return 0.0f;

	const bool do_locking = FSUSB2N_LOCK_ON_SIGNAL && m_USBEP.dev && m_USBEP.lockunlockFunc ;
    
	if(do_locking) //# lock
		m_USBEP.lockunlockFunc(m_USBEP.dev,1);

	float db = pDev->DeMod_GetQuality() * 0.01f;

	if(do_locking) //# unlock
		m_USBEP.lockunlockFunc(m_USBEP.dev,0);

	return db ;
}

void CBonTuner::Release()
{
	// インスタンス開放
	delete this;
}

LPCTSTR CBonTuner::GetTunerName(void)
{
	// チューナ名を返す
	return TEXT("FSUSB2N");
}

const BOOL CBonTuner::IsTunerOpening(void)
{
	return m_hUsbDev ? TRUE : FALSE;
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if(NULL == pDev) return FALSE;
	if(NULL == tsthr) return FALSE;

	
    DWORD dwFreq = GetChannel(dwSpace,dwChannel).Freq ;

	if(dwFreq < 90000U || dwFreq > 772000U) return FALSE;

	// Channel変更

	FifoStop();
	::Sleep(FSUSB2N_INTERIM_WAIT);

	const bool do_locking = FSUSB2N_LOCK_ON_SIGNAL && m_USBEP.dev && m_USBEP.lockunlockFunc ;
    
	if(do_locking) //# lock
		m_USBEP.lockunlockFunc(m_USBEP.dev,1);

	for(DWORD i=FSUSB2N_SETFREQ_TIMES;i;i--) {
		pDev->SetFrequency(dwFreq);
		::Sleep(FSUSB2N_INTERIM_WAIT);
	}
	for(DWORD i=FSUSB2N_RESETDEMOD_TIMES;i;i--) {
		pDev->ResetDeMod();
		::Sleep(FSUSB2N_INTERIM_WAIT);
	}

	if(do_locking) //# unlock
		m_USBEP.lockunlockFunc(m_USBEP.dev,0);

	FifoStart();
	::Sleep(FSUSB2N_INTERIM_WAIT);

	PurgeTsStream();


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

void CBonTuner::LoadValues(const IValueLoader *Loader)
{
	DWORD FunctionMode = DWORD(EM2874Device::UserSettings&0xffff) | DWORD(KtvDevice::UserSettings)<<16 ;

	CBonFSHybrid::LoadValues (Loader) ;
	#define LOADDW(val) do { val = Loader->ReadDWORD(L#val,val); } while(0)
	LOADDW(FunctionMode);
	LOADDW(FSUSB2N_INTERIM_WAIT);
	LOADDW(FSUSB2N_SETFREQ_TIMES);
	LOADDW(FSUSB2N_RESETDEMOD_TIMES);
	LOADDW(FSUSB2N_LOCK_ON_SIGNAL);
	#undef LOADDW

	EM2874Device::UserSettings = FunctionMode & 0xffff;
	KtvDevice::UserSettings = FunctionMode >> 16;
}

} // End of namespace FSUSB2N

