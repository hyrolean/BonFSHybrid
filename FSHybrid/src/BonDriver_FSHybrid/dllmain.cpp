//# 2019-12-21
//#   BonDriver_FSUSB2N.dll / BonDriver_FSUSB2i.dll / BonDriver_uSUNpTV.dll
//#   Hybrid dll entry point.
//# Coded by 2019 LVhJPic0JSk5LiQ1ITskKVk9UGBg
#include "stdafx.h"
#include <cstring>
#include <string>
#include "IBonDriver2.h"
#include "../BonDriver_FSUSB2N/BonTuner_FSUSB2N.h"
#include "../BonDriver_FSUSB2i/BonTuner_FSUSB2i.h"
#include "../BonDriver_uSUNpTV/BonTuner_uSUNpTV.h"
#include "../pryutil.h"

using namespace std ;

enum BONDRIVER_FSHYBRID {
	BONDRIVER_UNKNOWN,
	BONDRIVER_FSUSB2N,
	BONDRIVER_FSUSB2I,
	BONDRIVER_USUNPTV,
};

BONDRIVER_FSHYBRID FSHybrid = BONDRIVER_UNKNOWN;

void FSIdentify(HMODULE hModule)
{
	char path[_MAX_PATH] ;
	GetModuleFileNameA( hModule, path, _MAX_PATH ) ;
	std::string prefix = upper_case(file_prefix_of(path)) ;
	#define IDENTIFY(name) do { \
		if(prefix.substr(0,strlen(#name))==string(#name)) \
		{FSHybrid=name;return;} } while(0)
	IDENTIFY(BONDRIVER_FSUSB2N) ;
	IDENTIFY(BONDRIVER_FSUSB2I) ;
	IDENTIFY(BONDRIVER_USUNPTV) ;
	#undef IDENTIFY
	FSHybrid = BONDRIVER_UNKNOWN;
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		FSIdentify(hModule) ;
		FSUSB2N::CBonTuner::m_hModule = hModule;
		FSUSB2i::CBonTuner::m_hModule = hModule;
		uSUNpTV::CBonTuner::m_hModule = hModule;
		break;
	case DLL_PROCESS_DETACH:
		if(FSUSB2N::CBonTuner::m_pThis)FSUSB2N::CBonTuner::m_pThis->Release();
		if(FSUSB2i::CBonTuner::m_pThis)FSUSB2i::CBonTuner::m_pThis->Release();
		if(uSUNpTV::CBonTuner::m_pThis)uSUNpTV::CBonTuner::m_pThis->Release();
		break;
	}
	return TRUE;
}

#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver *CreateBonDriver()
{
	switch(FSHybrid) {
		case BONDRIVER_FSUSB2N:
			return (FSUSB2N::CBonTuner::m_pThis)?
				FSUSB2N::CBonTuner::m_pThis:
				((IBonDriver*) new FSUSB2N::CBonTuner);
		case BONDRIVER_FSUSB2I:
			return (FSUSB2i::CBonTuner::m_pThis)?
				FSUSB2i::CBonTuner::m_pThis:
				((IBonDriver*) new FSUSB2i::CBonTuner);
		case BONDRIVER_USUNPTV:
			return (uSUNpTV::CBonTuner::m_pThis)?
				uSUNpTV::CBonTuner::m_pThis:
				((IBonDriver*) new uSUNpTV::CBonTuner);
	}
	return NULL ;
}
#pragma warning( default : 4273 )

