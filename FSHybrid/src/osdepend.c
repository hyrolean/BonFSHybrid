/* fsusb2i   (c) 2015 trinity19683
  OS dependent (MS-Windows)
  osdepend.c
  2015-12-12
*/

#include "stdafx.h"
#include "osdepend.h"

#define UTHREAD_MUTEX_AS_CRITICAL_SECTION

void miliWait(unsigned msec)
{
	Sleep(msec);
}

void* uHeapAlloc(size_t sz)
{
	return VirtualAlloc( NULL, sz, MEM_COMMIT, PAGE_READWRITE );
}

void uHeapFree(void* const ptr)
{
	VirtualFree( ptr, 0, MEM_RELEASE );
}

int uthread_mutex_init(PMUTEX *p)
{
#ifdef UTHREAD_MUTEX_AS_CRITICAL_SECTION
	if(NULL == p)
		return -1;
	if(NULL == *p) {
		CRITICAL_SECTION *sec_p=uHeapAlloc(sizeof(CRITICAL_SECTION));
		if(sec_p==NULL) return ERROR_NOT_ENOUGH_MEMORY;
		InitializeCriticalSection(sec_p);
		*p=(PMUTEX)sec_p;
	}
	return 0;
#else
	if(NULL == p)
		return -1;
	if(NULL == *p) {
		*p = CreateMutex(NULL, FALSE, NULL);
		if(NULL == *p) return GetLastError();
	}
	return 0;
#endif
}

int uthread_mutex_lock(PMUTEX p)
{
#ifdef UTHREAD_MUTEX_AS_CRITICAL_SECTION
	if(p==NULL) return ERROR_INVALID_HANDLE;
	EnterCriticalSection((CRITICAL_SECTION*)p) ;
	return 0;
#else
	const DWORD dRet = WaitForSingleObject(p, 10000);
	if(WAIT_FAILED == dRet) return GetLastError();
	return dRet;
#endif
}

int uthread_mutex_unlock(PMUTEX p)
{
#ifdef UTHREAD_MUTEX_AS_CRITICAL_SECTION
	if(p==NULL) return ERROR_INVALID_HANDLE;
	LeaveCriticalSection((CRITICAL_SECTION*)p) ;
	return 0;
#else
	if(ReleaseMutex(p) == 0) return GetLastError();
	return 0;
#endif
}

int uthread_mutex_destroy(PMUTEX p)
{
#ifdef UTHREAD_MUTEX_AS_CRITICAL_SECTION
	if(p==NULL) return ERROR_INVALID_HANDLE;
	DeleteCriticalSection((CRITICAL_SECTION*)p);
	uHeapFree(p);
	return 0;
#else
	if(CloseHandle(p) == 0) return GetLastError();
	return 0;
#endif
}

/*EOF*/