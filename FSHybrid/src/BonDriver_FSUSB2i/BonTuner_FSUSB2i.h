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

#include "../pryutil.h"

namespace FSUSB2i {

class CBonTuner : public IBonDriver2
{
public:
    // CHANNEL/CHANNELS
    struct CHANNEL {
        std::wstring Space ;
        DWORD       Channel;
        float       MegaHz ;
        std::wstring	Name ;
        bool isChannelTuning() { return Channel!=0 ; }
        bool isMegaHzTuning() { return !isChannelTuning() ; }
        CHANNEL(std::wstring space, int channel, std::wstring name, float megaHz=0.f) {
            Space = space ;
            Channel = channel ;
            Name = name ;
            MegaHz = megaHz ;
        }
        CHANNEL(const CHANNEL &src) {
            Space = src.Space ;
            Channel = src.Channel ;
            Name = src.Name ;
            MegaHz = src.MegaHz ;
        }
    } ;
    typedef std::vector<CHANNEL> CHANNELS ;

private:
    bool UserChannelExists() { return !m_UserChannels.empty() ; }
    void LoadUserChannels() ;
    int UserDecidedDeviceIdx() ;
    std::string ModuleFileName() ;

public:
	CBonTuner();
	virtual ~CBonTuner();

//# IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);

	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);

	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);

	void PurgeTsStream(void);

//# IBonDriver2
	LPCTSTR GetTunerName(void);

	const BOOL IsTunerOpening(void);

	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);

	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);

	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

	void Release(void);

	static CBonTuner * m_pThis;
	static HINSTANCE m_hModule;

protected:
    DWORD m_dwCurSpace;
	DWORD m_dwCurChannel;
	DWORD *m_ChannelList;
    CHANNELS m_UserChannels ;
    std::vector<size_t> m_UserSpaceIndices;

	HANDLE m_hDev;
	HANDLE m_hUsbDev;
	struct usb_endpoint_st  m_USBEP;
	it9175_state pDev;
	tsthread_ptr tsthr;

    CAsyncFifo *m_fifo ;
    CAsyncFifo::CACHE *m_mapCache[TS_MaxNumIO] ;
    event_object m_eoCaching ;
    exclusive_object m_coPurge ;
    static void *OnWriteBackBegin(int id, size_t max_size, void *arg) ;
    static void OnWriteBackFinish(int id, size_t wrote_size, void *arg) ;
    static void OnWriteBackPurge(void *arg) ;

	bool LoadData ();
	void ReadRegMode (HKEY hPKey);
	void ReadRegChannels (HKEY hPKey);
};

} // End of namespace FSUSB2i

