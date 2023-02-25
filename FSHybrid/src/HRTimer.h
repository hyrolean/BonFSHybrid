//===========================================================================
#pragma once

#ifndef _HRTIMER_187BE8C9_0DB1_4F50_B02E_B271FCB3B274_H_INCLUDED_
#define _HRTIMER_187BE8C9_0DB1_4F50_B02E_B271FCB3B274_H_INCLUDED_
//---------------------------------------------------------------------------

#define PRY8EAlByw_HRTIMER

#ifdef __cplusplus
//===========================================================================
namespace PRY8EAlByw {
//---------------------------------------------------------------------------

  // High Resolution Timer functions

void SetHRTimerMode(BOOL useHighResolution) ;
void HRSleep(DWORD msec, DWORD usec=0) ;
DWORD HRWaitForSingleObject(HANDLE hObj, DWORD msec, DWORD usec=0);
DWORD HRWaitForMultipleObjects(DWORD numObjs, const HANDLE *hObjs, BOOL waitAll,
  DWORD msec, DWORD usec=0);

//---------------------------------------------------------------------------
} // End of namespace PRY8EAlByw
//===========================================================================
using namespace PRY8EAlByw ;
//===========================================================================
#endif
//===========================================================================
#ifdef __cplusplus
extern "C" {
#endif

void SetHRTimerMode_C(BOOL useHighResolution) ;
void HRSleep_C(DWORD msec, DWORD usec);
DWORD HRWaitForSingleObject_C(HANDLE hObj, DWORD msec, DWORD usec);
DWORD HRWaitForMultipleObjects_C(DWORD numObjs, const HANDLE *hObjs, BOOL waitAll,
  DWORD msec, DWORD usec);

#ifndef __cplusplus

#define SetHRTimerMode				SetHRTimerMode_C
#define HRSleep						HRSleep_C
#define HRWaitForSingleObject		HRWaitForSingleObject_C
#define HRWaitForMultipleObjects	HRWaitForMultipleObjects_C

#endif

#ifdef __cplusplus
} // End of extern "C"
#endif
//===========================================================================
#endif // _HRTIMER_187BE8C9_0DB1_4F50_B02E_B271FCB3B274_H_INCLUDED_
