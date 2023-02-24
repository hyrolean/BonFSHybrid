#pragma once

// BonTuner.h: CBonTuner クラスのインターフェイス
//////////////////////////////////////////////////

#include "IBonDriver2.h"
#include "../ktv.h"
#include "../pryutil.h"

#ifndef NO_TSTHREAD
#include <vector>
#include <queue>
#include <cstdlib>
#endif

namespace FSUSB2N {

class CBonTuner : public IBonDriver2
{
private:
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

// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);

	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);

	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);

	void PurgeTsStream(void);

// IBonDriver2(暫定)
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

	EM2874Device *usbDev;
	KtvDevice *pDev;
	BYTE *m_pTsBuff;
	volatile int *m_TsBuffSize;		// 32bit signed
	volatile int m_indexTsBuff;

	HANDLE m_hThread;
	unsigned int TsThreadMain () ;
	static unsigned int __stdcall TsThread (PVOID pv);
	// Asynchronized fifo buffering based contexts
	CAsyncFifo *m_fifo ;
	CAsyncFifo::CACHE *m_mapCache[NUM_IOHANDLE] ;
	event_object m_eoCaching ;
	exclusive_object m_coPurge ;
	static void *OnWriteBackBegin(int id, size_t max_size, void *arg) ;
	static void OnWriteBackFinish(int id, size_t wrote_size, void *arg) ;
	exclusive_object m_exSuspend ;
	HANDLE m_evThreadSuspend ;
	HANDLE m_evThreadResume ;
	int m_cntThreadSuspend ;
	void ThreadSuspend() ;
	void ThreadResume() ;

	bool LoadData ();
	void ReadRegMode (HKEY hPKey);
	void ReadRegChannels (HKEY hPKey);
};

} // End of namespace FSUSB2N

