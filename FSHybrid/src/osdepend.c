/* fsusb2i   (c) 2015 trinity19683
  OS dependent (MS-Windows)
  osdepend.c
  2015-12-12
*/

#include "stdafx.h"
#include "osdepend.h"

#define UTHREAD_MUTEX_AS_CRITICAL_SECTION
//#define UHEAP_AS_VIRTUAL

void miliWait(unsigned msec)
{
	HRSleep(msec,0);
}

void* uHeapAlloc(size_t sz)
{
#ifdef UHEAP_AS_VIRTUAL
	return VirtualAlloc( NULL, sz, MEM_COMMIT, PAGE_READWRITE );
#else
	return HeapAlloc( GetProcessHeap(), 0, sz );
#endif
}

void uHeapFree(void* const ptr)
{
#ifdef UHEAP_AS_VIRTUAL
	VirtualFree( ptr, 0, MEM_RELEASE );
#else
	HeapFree( GetProcessHeap(), 0, ptr );
#endif
}

int uthread_mutex_init(PMUTEX *p)
{
#ifdef UTHREAD_MUTEX_AS_CRITICAL_SECTION
	if(NULL == p)
		return -1;
	if(NULL == *p) {
		CRITICAL_SECTION *sec_p = (CRITICAL_SECTION*) uHeapAlloc(sizeof(CRITICAL_SECTION)) ;
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
	const DWORD dRet = HRWaitForSingleObject(p, 10000,0 );
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