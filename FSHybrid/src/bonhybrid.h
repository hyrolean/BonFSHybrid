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

DWORD RegReadDword(HKEY hKey,LPCTSTR name,DWORD defVal=0);


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
		std::wstring Space ;
		BAND        Band ;
		WORD	    Stream ;
		WORD        TSID ;
		DWORD       Freq ;
		std::wstring	Name ;
		CHANNEL();
        CHANNEL(std::wstring space, BAND band, int channel, std::wstring name,unsigned stream=0,unsigned tsid=0);
        CHANNEL(std::wstring space, DWORD freq,std::wstring name,unsigned stream=0,unsigned tsid=0);
		CHANNEL(const CHANNEL &src) ;
		static DWORD FreqFromBandCh(BAND band,int ch);
        static BAND BandFromFreq(DWORD freq);
	} ;
	typedef std::vector<CHANNEL> CHANNELS ;

protected:
	std::string ModuleFileName() ;
	// Device
    virtual int UserDecidedDeviceIdx() { return -1 ; }
    bool FindDevice(const GUID &drvGUID, HANDLE &hDev, HANDLE &hUsbDev) ;
    void FreeDevice(HANDLE &hDev, HANDLE &hUsbDev) ;
    // Channels
    bool UserChannelExists() { return !m_UserChannels.empty() ; }
	void LoadUserChannels(bool hasSatellite) ;
	CHANNEL *GetUserChannel(DWORD dwSpace, DWORD dwChannel);
    CHANNEL GetChannel(DWORD dwSpace, DWORD dwChannel);
	DWORD GetTerraFreq(DWORD dwSpace, DWORD dwChannel);
    // FIFO
	bool FifoInitialize(usb_endpoint_st *usbep) ;
	void FifoFinalize() ;
	void FifoStart() ;
	void FifoStop() ;
	static void *OnWriteBackBegin(int id, size_t max_size, void *arg) ;
	static void OnWriteBackFinish(int id, size_t wrote_size, void *arg) ;
	static void OnWriteBackPurge(void *arg) ;

public:
	// Registry
	void ReadRegMode (HKEY hPKey);
	void ReadRegChannels (HKEY hPKey);
protected:
	// Data Loader [ no virtual ]
    // ( It will be called inline direct from the constructor because of the virtual functions aren't able to work yet. )
    template<class T> void LoadData(T this_, const TCHAR* reg_, bool hasS_=false){
		HKEY hKey;
		if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_LOCAL_MACHINE, reg_, 0, KEY_READ, &hKey)) {
			this_->ReadRegMode(hKey);
			this_->ReadRegChannels(hKey);
			RegCloseKey(hKey);
		}
		if(ERROR_SUCCESS == RegOpenKeyEx( HKEY_CURRENT_USER, reg_, 0, KEY_READ, &hKey)) {
			this_->ReadRegMode(hKey);
			this_->ReadRegChannels(hKey);
			RegCloseKey(hKey);
		}
		this_->LoadUserChannels(hasS_) ;
	}

public: // inherited
	// IBonDriver
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

public:
	CBonFSHybrid();
	virtual ~CBonFSHybrid();

protected:
    // Extra Channel List
	DWORD *m_RegChannels;
	// User Channels
	CHANNELS m_UserChannels ;
	std::vector<size_t> m_UserSpaceIndices;
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

//===========================================================================
#endif // _BONHYBRID_20191229125131957_H_INCLUDED_
