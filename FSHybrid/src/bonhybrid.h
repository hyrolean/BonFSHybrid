//===========================================================================
#pragma once

#ifndef _BONHYBRID_20191229125131957_H_INCLUDED_
#define _BONHYBRID_20191229125131957_H_INCLUDED_
//---------------------------------------------------------------------------

#include <string>
#include <vector>
#include "pryutil.h"
#include "IBonDriver2.h"

extern "C" {
#include "tsbuff.h"
#include "tsthread.h"
}

#define SPACE_CHASFREQ  114514

namespace BonHybrid {

class IValueLoader
{
public:
    virtual DWORD ReadDWORD(const std::wstring name,DWORD defVal=0) const =0 ;
	virtual std::wstring ReadString(const std::wstring name,const std::wstring defStr=L"") const =0 ;
};

class CBonFSHybrid : public IBonDriver2
{
public:
	// BAND
	enum BAND {
		BAND_na, // [n/a]
		BAND_VU, // VHS, UHF or CATV
		BAND_BS, // BS
		BAND_ND  // CS110
	};
	// CHANNEL/CHANNELS
	struct CHANNEL {
		std::wstring	Space ;
		std::wstring	Name ;
		BAND			Band ;
		DWORD			Freq ;
		unsigned		Stream:3 ;
		WORD			TSID ;
		CHANNEL();
		CHANNEL(std::wstring space, BAND band, int channel, std::wstring name,unsigned stream=0,unsigned tsid=0);
		CHANNEL(std::wstring space, DWORD freq,std::wstring name,unsigned stream=0,unsigned tsid=0);
		CHANNEL(const CHANNEL &src) ;
		static DWORD FreqFromBandCh(BAND band,int ch);
		static BAND BandFromFreq(DWORD freq);
	} ;
	typedef std::vector<CHANNEL> CHANNELS ;
	typedef std::vector<size_t> SPACEINDICES ;
	typedef std::vector<std::wstring> SPACENAMES ;
protected:
	std::string ModuleFileName() ;
	// Device
	virtual int UserDecidedDeviceIdx() { return -1 ; }
	bool FindDevice(const GUID &drvGUID, HANDLE &hDev, HANDLE &hUsbDev) ;
	void FreeDevice(HANDLE &hDev, HANDLE &hUsbDev) ;
	// Channels
	void LoadUserChannels() ;
	void BuildTChannels() ;
	void BuildSChannels() ;
	void BuildAuxChannels() ;
	void ArrangeChannels() ;
	CHANNEL *GetUserChannel(DWORD dwSpace, DWORD dwChannel);
	CHANNEL GetChannel(DWORD dwSpace, DWORD dwChannel);
	// FIFO
	bool FifoInitialize(usb_endpoint_st *usbep) ;
	void FifoFinalize() ;
	void FifoStart() ;
	void FifoStop() ;
	static void *OnTSFifoWriteBackBegin(int id, size_t max_size, void *arg) ;
	static void OnTSFifoWriteBackFinish(int id, size_t wrote_size, void *arg) ;
	static void OnTSFifoWriteThrough(const void *buffer, size_t size, void *arg) ;
	static void OnTSFifoPurge(void *arg) ;

protected:
	// Initializer
	virtual void InitConstants();
	virtual const BOOL TryOpenTuner(void) = 0 ;
	// Registry
	virtual const TCHAR *RegName() { return NULL ; }
	// Channels
	virtual void ReadRegChannels (HKEY hPKey, CHANNELS &regChannels);
	virtual void ReadIniChannels (const std::string iniFilename, CHANNELS &iniChannels);
	// Loader
	virtual void LoadReg();
	virtual void LoadIni();
	virtual void LoadValues(const IValueLoader *Loader);

public:
	// Initializer
	virtual void Initialize();

public: // inherited
	// IBonDriver
	virtual const BOOL OpenTuner(void);
	virtual const BOOL SetChannel(const BYTE bCh);
	virtual const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	virtual const DWORD GetReadyCount(void);
	virtual const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	virtual const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);
	virtual void PurgeTsStream(void);
	// IBonDriver2
	virtual LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	virtual LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);
	virtual const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) {return FALSE;}

protected:
	CBonFSHybrid(bool hasSatellite=false);
	virtual ~CBonFSHybrid();

protected:
	// User Channels
	CHANNELS m_UserChannels ;
	SPACEINDICES m_UserSpaceIndices;
	SPACENAMES m_SpaceArrangement, m_InvisibleSpaces ;
	bool m_hasSatellite ;
	// tsthread
	tsthread_ptr tsthr;
	// FIFO
	CAsyncFifo *m_fifo ;
	CAsyncFifo::CACHE *m_mapCache[TS_MaxNumIO] ;
	event_object m_eoCaching ;
	exclusive_object m_coPurge ;

public:
	// Instance
	static CBonFSHybrid *m_pThis;
	static HINSTANCE m_hModule;

};

template<class T> IBonDriver *BonFSCreate() {
	if(T::m_pThis) return static_cast<IBonDriver*>(T::m_pThis) ;
	if(T *bon = new T) { bon->Initialize() ; return static_cast<IBonDriver*>(bon) ; }
	return 0 ;
}

} // End of namespace BonHybrid

using namespace BonHybrid ;

//===========================================================================
#endif // _BONHYBRID_20191229125131957_H_INCLUDED_
