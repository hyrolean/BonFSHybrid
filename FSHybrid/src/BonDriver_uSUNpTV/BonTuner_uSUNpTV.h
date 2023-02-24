/* SunPTV-USB   (c) 2016 trinity19683
  BonTuner.DLL (MS-Windows)
  BonTuner.h
  2016-01-23
*/
#pragma once

#include "../inc/IBonDriver2.h"
extern "C" {
#include "../em287x.h"
#include "../tsthread.h"
#include "../tsbuff.h"
}

#include "../pryutil.h"

namespace uSUNpTV {

class CBonTuner : public IBonDriver2
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
		CHANNEL(std::wstring space, BAND band, int channel, std::wstring name,unsigned stream=0,unsigned tsid=0) {
			Space = space ;
			Band = band ;
			Name = name ;
			Freq = FreqFromBandCh(band,channel) ;
			Stream = stream ;
            TSID = tsid ;
		}
		CHANNEL(std::wstring space, DWORD freq,std::wstring name,unsigned stream=0,unsigned tsid=0) {
			Space = space ;
			Band = BandFromFreq(freq) ;
			Name = name ;
			Freq = freq ;
			Stream = stream ;
            TSID = tsid ;
		}
		CHANNEL(const CHANNEL &src) {
			Space = src.Space ;
			Band = src.Band ;
			Name = src.Name ;
			Freq = src.Freq ;
			Stream = src.Stream ;
            TSID = src.TSID;
		}
		static DWORD FreqFromBandCh(BAND band,int ch) {
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
		static BAND BandFromFreq(DWORD freq) {
			if(freq < 60000UL || freq > 2456123UL )
				return BAND_na ;
			if(freq < 900000UL )
				return BAND_VU ;
			if(freq < 1573000UL )
				return BAND_BS ;
			return BAND_ND ;
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
	BAND m_bandCur ;
	unsigned m_streamCur ;
    WORD m_tsidCur ;
	DWORD  m_freqCur ;

	HANDLE m_hDev;
	HANDLE m_hUsbDev;
	struct usb_endpoint_st  m_USBEP;
	em287x_state pDev;
	void* demodDev;
	void* tunerDev[2];
	int m_selectedTuner;
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

} // End of namespace uSUNpTV

