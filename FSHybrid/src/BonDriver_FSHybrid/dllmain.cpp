//# 2020-1-3
//#   BonDriver_FSUSB2N.dll / BonDriver_FSUSB2i.dll / BonDriver_uSUNpTV.dll
//#   Hybrid dll entry point.
//# Coded by 2019-2020 LVhJPic0JSk5LiQ1ITskKVk9UGBg
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
		CBonFSHybrid::m_hModule = hModule;
		break;
	case DLL_PROCESS_DETACH:
		if(CBonFSHybrid::m_pThis)CBonFSHybrid::m_pThis->Release();
		break;
	}
	return TRUE;
}

#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver *CreateBonDriver()
{
	switch(FSHybrid) {
		case BONDRIVER_FSUSB2N:
			return BonFSCreate<FSUSB2N::CBonTuner>();
		case BONDRIVER_FSUSB2I:
			return BonFSCreate<FSUSB2i::CBonTuner>();
		case BONDRIVER_USUNPTV:
			return BonFSCreate<uSUNpTV::CBonTuner>();
	}
	return NULL ;
}
#pragma warning( default : 4273 )

