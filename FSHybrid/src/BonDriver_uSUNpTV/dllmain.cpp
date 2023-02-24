#include "stdafx.h"
#include "BonTuner_uSUNpTV.h"

using namespace uSUNpTV;

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		CBonTuner::m_hModule = hModule;
		break;
	case DLL_PROCESS_DETACH:
		if(CBonTuner::m_pThis)CBonTuner::m_pThis->Release();
		break;
	}
	return TRUE;
}

#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver *CreateBonDriver()
{return (CBonTuner::m_pThis)?CBonTuner::m_pThis:((IBonDriver*) new CBonTuner);}
#pragma warning( default : 4273 )

