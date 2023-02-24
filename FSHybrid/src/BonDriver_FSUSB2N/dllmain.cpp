// dllmain.cpp : DLL アプリケーションのエントリ ポイントを定義します。
#include "stdafx.h"
#include "BonTuner_FSUSB2N.h"

using namespace FSUSB2N;

BOOL APIENTRY DllMain( HMODULE hModule,
					   DWORD  ul_reason_for_call,
					   LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		// モジュールハンドル保存
		CBonTuner::m_hModule = hModule;
		break;
	case DLL_PROCESS_DETACH:
		// 未開放の場合はインスタンス開放
		if(CBonTuner::m_pThis)CBonTuner::m_pThis->Release();
		break;
	}
	return TRUE;
}

#pragma warning( disable : 4273 )
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{return (CBonTuner::m_pThis)?CBonTuner::m_pThis:((IBonDriver*) new CBonTuner);}
#pragma warning( default : 4273 )

