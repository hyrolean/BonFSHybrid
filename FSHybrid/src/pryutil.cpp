//===========================================================================
#include "stdafx.h"
#include <process.h>
#include <locale.h>

#include "pryutil.h"
//---------------------------------------------------------------------------
using namespace std ;

//===========================================================================
namespace PRY8EAlByw {
//---------------------------------------------------------------------------

//===========================================================================
// Functions
//---------------------------------------------------------------------------
DWORD Elapsed(DWORD start, DWORD end)
{
  DWORD result = (end>=start) ? end-start : MAXDWORD-end+1+start ;
  return result ;
}
//---------------------------------------------------------------------------
DWORD PastSleep(DWORD wait,DWORD start)
{
  if(!wait) return start ;
  DWORD past = Elapsed(start,GetTickCount()) ;
  if(wait>past) Sleep(wait-past) ;
  return start+wait ;
}
//---------------------------------------------------------------------------
wstring mbcs2wcs(string src)
{
    if(src.empty()) return wstring(L"") ;
    BUFFER<wchar_t> wcs(src.length()*3 + 3);
    size_t wLen = 0;
    setlocale(LC_ALL,"japanese");
    mbstowcs_s(&wLen, wcs.data(), src.length()*3+1 , src.c_str(), _TRUNCATE);
    wstring result = wcs.data() ;
    return result ;
}
//---------------------------------------------------------------------------
string wcs2mbcs(wstring src)
{
    if(src.empty()) return string("") ;
    BUFFER<char> mbcs(src.length()*3 + 3) ;
    size_t mbLen = 0 ;
    setlocale(LC_ALL,"japanese");
    wcstombs_s(&mbLen, mbcs.data(), src.length()*3+1 , src.c_str(), _TRUNCATE);
    string result = mbcs.data() ;
    return result ;
}
//---------------------------------------------------------------------------
string itos(int val,int radix)
{
  BUFFER<char> str(72) ;
  if(!_itoa_s(val,str.data(),70,radix))
    return static_cast<string>(str.data()) ;
  return "NAN" ;
}
//---------------------------------------------------------------------------
wstring itows(int val,int radix)
{
  BUFFER<wchar_t> str(72) ;
  if(!_itow_s(val,str.data(),70,radix))
    return static_cast<wstring>(str.data()) ;
  return L"NAN" ;
}
//---------------------------------------------------------------------------
string upper_case(string str)
{
  BUFFER<char> temp(str.length()+1) ;
  CopyMemory(temp.data(),str.c_str(),(str.length()+1)*sizeof(char)) ;
  _strupr_s(temp.data(),str.length()+1) ;
  return static_cast<string>(temp.data()) ;
}
//---------------------------------------------------------------------------
string lower_case(string str)
{
  BUFFER<char> temp(str.length()+1) ;
  CopyMemory(temp.data(),str.c_str(),(str.length()+1)*sizeof(char)) ;
  _strlwr_s(temp.data(),str.length()+1) ;
  return static_cast<string>(temp.data()) ;
}
//---------------------------------------------------------------------------
string file_path_of(string filename)
{
  char szDrive[_MAX_FNAME] ;
  char szDir[_MAX_FNAME] ;
  _splitpath_s( filename.c_str(), szDrive, _MAX_FNAME, szDir, _MAX_FNAME
    , NULL, 0, NULL, 0 ) ;
  return string(szDrive)+string(szDir) ;
}
//---------------------------------------------------------------------------
string file_prefix_of(string filename)
{
  char szName[_MAX_FNAME] ;
  _splitpath_s( filename.c_str(), NULL, 0, NULL, 0
    , szName, _MAX_FNAME, NULL, 0 ) ;
  return string(szName) ;
}
//===========================================================================
// event_object
//---------------------------------------------------------------------------
static int event_create_count = 0 ;
//---------------------------------------------------------------------------
event_object::event_object(BOOL _InitialState,wstring _name)
{
  if(_name.empty()) _name = L"event"+itows(event_create_count++) ;
  name = _name ;
  event = CreateEvent(NULL,FALSE,_InitialState,name.c_str()) ;
#ifdef _DEBUG
  if(is_valid()) {
    TRACE(L"event_object created. [name=%s]\r\n",name.c_str()) ;
  }else {
    TRACE(L"event_object failed to create. [name=%s]\r\n",name.c_str()) ;
  }
#endif
}
//---------------------------------------------------------------------------
event_object::event_object(const event_object &clone_source)
{
  name = clone_source.name ;
  event = clone_source.open() ;
#ifdef _DEBUG
  if(is_valid()) {
    TRACE(L"event_object cloned. [name=%s]\r\n",name.c_str()) ;
  }else {
    TRACE(L"event_object failed to clone. [name=%s]\r\n",name.c_str()) ;
  }
#endif
}
//---------------------------------------------------------------------------
event_object::~event_object()
{
#ifdef _DEBUG
  if(is_valid()) {
    TRACE(L"event_object finished. [name=%s]\r\n",name.c_str()) ;
  }else {
    TRACE(L"event_object finished. (failure) [name=%s]\r\n",name.c_str()) ;
  }
#endif
  if(is_valid()) CloseHandle(event) ;
}
//---------------------------------------------------------------------------
HANDLE event_object::open() const
{
  if(!is_valid()) return NULL ;
  HANDLE open_event = OpenEvent(EVENT_ALL_ACCESS, FALSE, name.c_str());
  return open_event ;
}
//---------------------------------------------------------------------------
DWORD event_object::wait(DWORD timeout)
{
  return is_valid() ? WaitForSingleObject(event,timeout) : WAIT_FAILED ;
}
//---------------------------------------------------------------------------
BOOL event_object::set()
{
  return is_valid() ? SetEvent(event) : FALSE ;
}
//---------------------------------------------------------------------------
BOOL event_object::reset()
{
  return is_valid() ? ResetEvent(event) : FALSE ;
}
//---------------------------------------------------------------------------
BOOL event_object::pulse()
{
  return is_valid() ? PulseEvent(event) : FALSE ;
}
//===========================================================================
// critical_object
//---------------------------------------------------------------------------
critical_object::critical_object()
{
  ref = new critical_ref_t ;
  InitializeCriticalSection(&ref->critical) ;
}
//---------------------------------------------------------------------------
critical_object::critical_object(const critical_object &clone_source)
{
  ref = clone_source.ref ;
  assert(ref!=NULL);
  enter();
  ref->ref_count++ ;
  leave();
}
//---------------------------------------------------------------------------
critical_object::~critical_object()
{
  enter();
  bool empty = !--ref->ref_count ;
  leave();
  if(empty) {
    DeleteCriticalSection(&ref->critical) ;
    delete ref ;
  }
}
//---------------------------------------------------------------------------
void critical_object::enter()
{
  EnterCriticalSection(&ref->critical) ;
}
//---------------------------------------------------------------------------
BOOL critical_object::try_enter()
{
  return TryEnterCriticalSection(&ref->critical) ;
}
//---------------------------------------------------------------------------
void critical_object::leave()
{
  LeaveCriticalSection(&ref->critical) ;
}
//===========================================================================
// CAsyncFifo
//---------------------------------------------------------------------------
CAsyncFifo::CAsyncFifo(
  size_t initialPool, size_t maximumPool, size_t emptyBorder,
  size_t packetSize, DWORD threadWait,int threadPriority)
  : MaximumPool(max(1,max(initialPool,maximumPool))),
    TotalPool(min(max(1,initialPool),MaximumPool)),
	Writing(WRITING_NONE),
    Indices(MaximumPool),
    EmptyIndices(MaximumPool),
    EmptyBorder(emptyBorder),
	EmptyLimit(0),
    PacketSize(packetSize),
    THREADWAIT(threadWait),
    AllocThread(NULL),
    AllocOrderEvent(FALSE),
    AllocatedEvent(FALSE),
	ModerateAllocating(true),
    Terminated(false)
{
    DWORD flag = HEAP_NO_SERIALIZE ;
    // バッファ初期化
    PacketSize = packetSize ;
    BufferPool.resize(MaximumPool);
#ifdef ASYNCFIFO_HEAPBUFFERPOOL
    // ヒープ作成
    Heap = HeapCreate(flag, 0, 0);
    BufferPool.set_heap(Heap) ;
    BufferPool.set_heap_flag(flag) ;
#endif
    for(size_t i = 0UL ; i < TotalPool ; i++){
        BufferPool[i].resize(PacketSize);
        EmptyIndices.push(i) ;
    }
#ifdef ASYNCFIFO_HEAPBUFFERPOOL
    //HeapCompact(Heap, flag) ;
#endif
    // アロケーションスレッド作成
    if(MaximumPool>TotalPool) {
      AllocThread = (HANDLE)_beginthreadex(NULL, 0, AllocThreadProc, this, CREATE_SUSPENDED, NULL) ;
      if(AllocThread == INVALID_HANDLE_VALUE) {
          AllocThread = NULL;
      }else{
          SetThreadPriority(AllocThread,threadPriority);
		  ::ResumeThread(AllocThread) ;
      }
    }
}
//---------------------------------------------------------------------------
CAsyncFifo::~CAsyncFifo()
{
    // アロケーションスレッド破棄
    Terminated=true ;
    bool abnormal=false ;
    if(AllocThread) {
      AllocOrderEvent.set() ;
      if(::WaitForSingleObject(AllocThread,30000) != WAIT_OBJECT_0) {
        ::TerminateThread(AllocThread, 0);
        abnormal=true ;
      }
      ::CloseHandle(AllocThread) ;
    }

#ifdef ASYNCFIFO_HEAPBUFFERPOOL
    // バッファ放棄（直後にヒープ自体を破棄するのでメモリリークは発生しない）
    BufferPool.abandon_erase(Heap) ;
    // ヒープ破棄
    if(!abnormal) HeapDestroy(Heap) ;
#endif
}
//---------------------------------------------------------------------------
CAsyncFifo::CACHE *CAsyncFifo::BeginWriteBack(bool allocWaiting)
{
  exclusive_lock plock(&PurgeExclusive,false) ;
  exclusive_lock elock(&Exclusive,false) ;

  if(!allocWaiting&&EmptyIndices.size()<=EmptyLimit) {
    AllocOrderEvent.set() ;
    return NULL ;
  }

  plock.lock();

  if(EmptyIndices.size()<=EmptyBorder) {
    if(allocWaiting) {
      if(!WaitForAllocation()&&EmptyIndices.empty())
        return NULL ;
    }else {
      // Allocation ordering...
      AllocOrderEvent.set() ;
    }
  }

  elock.lock();

  if(EmptyIndices.empty()) return NULL ;
  size_t index = EmptyIndices.front() ;
  EmptyIndices.pop() ;
  CACHE *cache = &BufferPool[index] ;
  WriteBackMap[cache] = index ;
  return cache;
}
//---------------------------------------------------------------------------
bool CAsyncFifo::FinishWriteBack(CAsyncFifo::CACHE *cache,bool fragment)
{
  exclusive_lock lock(&Exclusive) ;

  WRITEBACKMAP::iterator pos = WriteBackMap.find(cache) ;
  if(pos==WriteBackMap.end()) return false ;

  bool result = true ;

  size_t index = pos->second ;
  if(cache->size()>0) {
    if(fragment) {
      exclusive_lock plock(&Exclusive) ;
      if(Writing==WRITING_FRAGMENT) {
        Indices.push(WritingIndex) ;
        Writing=WRITING_NONE ;
      }
    }
    Indices.push(index) ;
  }else {
    EmptyIndices.push_front(index) ;
    result = false ;
  }
  WriteBackMap.erase(pos) ;

  return result ;
}
//---------------------------------------------------------------------------
size_t CAsyncFifo::Push(const BYTE *data, DWORD len, bool ignoreFragment,bool allocWaiting)
{
  if(!data||!len)
    return 0 ;

  exclusive_lock plock(&PurgeExclusive,false) ;
  exclusive_lock elock(&Exclusive,false) ;


  if(!allocWaiting&&EmptyIndices.size()<=EmptyLimit) {
    AllocOrderEvent.set() ;
    return 0 ;
  }

  plock.lock();

  size_t sz, n=0 ;
  for(BYTE *p = const_cast<BYTE*>(data) ; len ; len-=(DWORD)sz, p+=sz) {

    sz=min(len,PacketSize) ;

    if(Writing!=WRITING_FRAGMENT) {

      if(EmptyIndices.size()<=EmptyBorder) {
        if(allocWaiting) {
          if(!WaitForAllocation()&&EmptyIndices.empty())
            return n ;
        }else {
          // allocation ordering...
          AllocOrderEvent.set() ;
          if(EmptyIndices.size()<=EmptyLimit)
            return n ;
        }
      }

      elock.lock();

	  if(EmptyIndices.empty()) return n ;
      // get the empty index
      WritingIndex = EmptyIndices.front() ;
      EmptyIndices.pop() ;
      Writing = WRITING_PACKET ;

      elock.unlock();
    }

    // resize and data writing (no lock)
    switch(Writing) {
      case WRITING_FRAGMENT: { // fragmentation occurred
        size_t buf_sz = BufferPool[WritingIndex].size();
        sz = min(sz, PacketSize - buf_sz);
        BufferPool[WritingIndex].resize(buf_sz + sz);
        CopyMemory(BufferPool[WritingIndex].data() + buf_sz, p, sz);
        break;
      }
      case WRITING_PACKET:
        BufferPool[WritingIndex].resize(sz) ;
        CopyMemory(BufferPool[WritingIndex].data(), p, sz );
        break;
    }

        if (ignoreFragment || BufferPool[WritingIndex].size() == PacketSize) {
            // push to FIFO buffer
            elock.lock();
            Writing = WRITING_NONE;
            Indices.push(WritingIndex);
            elock.unlock();
            n++;
        }
        else
            Writing = WRITING_FRAGMENT;

  }
  return n ;
}
//---------------------------------------------------------------------------
bool CAsyncFifo::Pop(BYTE **data, DWORD *len,DWORD *remain)
{
    exclusive_lock lock(&Exclusive);
    if(Empty()) {
        if(len) *len = 0 ;
        if(data) *data = 0 ;
        if(remain) *remain = 0 ;
        return false ;
    }
    size_t index = Indices.front() ;
    if(len) *len = (DWORD)BufferPool[index].size() ;
    if(data) *data = (BYTE*)BufferPool[index].data() ;
    EmptyIndices.push(index) ;
    Indices.pop();
    if(remain) *remain = (DWORD)Size() ;
    return true;
}
//---------------------------------------------------------------------------
void CAsyncFifo::Purge(bool purgeWriteBack)
{
    // バッファから取り出し可能データをパージする
    exclusive_lock plock(&PurgeExclusive) ;
    exclusive_lock lock(&Exclusive);

    // 未処理のデータをパージする
    while(!Indices.empty()) {
        EmptyIndices.push(Indices.front()) ;
        Indices.pop() ;
    }
    if(Writing!=WRITING_NONE) {
		EmptyIndices.push(WritingIndex) ;
		Writing = WRITING_NONE ;
	}

    if(purgeWriteBack) {
        for(WRITEBACKMAP::iterator pos = WriteBackMap.begin() ;
         pos!= WriteBackMap.end() ; ++pos) {
            EmptyIndices.push(pos->second) ;
        }
        WriteBackMap.clear() ;
    }
}
//---------------------------------------------------------------------------
unsigned int CAsyncFifo::AllocThreadProcMain ()
{
    exclusive_lock elock(&Exclusive,false) ;
    for(;;) {
        DWORD dwRet = AllocOrderEvent.wait(THREADWAIT);
        if (Terminated) break;
        bool doAllocate = false;
        switch(dwRet) {
          	case WAIT_OBJECT_0: // Allocation ordered
            	elock.lock() ;
				doAllocate = Growable() ;
                if(EmptyIndices.size() > EmptyBorder)
                  AllocatedEvent.set() ;
            	elock.unlock() ;
				break ;
		  	case WAIT_TIMEOUT:
				if(!ModerateAllocating) {
            	  elock.lock() ;
				  doAllocate = Growable() && EmptyIndices.size() <= EmptyBorder ;
            	  elock.unlock() ;
				}
				break ;
			case WAIT_FAILED:
				return 1;
		}
		if(doAllocate) {
            bool failed=false ;
            do {
                elock.unlock() ;
				BufferPool[TotalPool].resize(PacketSize); // Allocating...
                if(BufferPool[TotalPool].size()!=PacketSize) {
                  failed = true ; break ;
                }
                elock.lock() ;
                EmptyIndices.push_front(TotalPool++) ;
				elock.unlock() ;
				AllocatedEvent.set();
                if (Terminated) break;
				if (ModerateAllocating) break;
				elock.lock() ;
			} while (Growable() && EmptyIndices.size() <= EmptyBorder );
            elock.unlock() ;
            if(failed)
              DBGOUT("Async FIFO allocation: allocation failed!\r\n") ;
            else
              DBGOUT("Async FIFO allocation: total %d bytes grown.\r\n",
  				  int((TotalPool)*PacketSize)) ;
        }
        if (Terminated) break;
    }
    return 0 ;
}
//---------------------------------------------------------------------------
unsigned int __stdcall CAsyncFifo::AllocThreadProc (PVOID pv)
{
    register CAsyncFifo *_this = static_cast<CAsyncFifo*>(pv) ;
    unsigned int result = _this->AllocThreadProcMain() ;
    _endthreadex(result) ;
    return result;
}

//---------------------------------------------------------------------------
bool CAsyncFifo::WaitForAllocation()
{
	exclusive_lock elock(&Exclusive) ;

	size_t n=EmptyIndices.size() ;
	if(n>EmptyBorder) {
	  return true ;
	}

    DBGOUT("Async FIFO allocation: allocation waiting...\r\n") ;

    bool result = false ;

      do {
		if (!Growable()) break ;
		elock.unlock() ;
		AllocOrderEvent.set();
		DWORD res = AllocatedEvent.wait(THREADWAIT) ;
		if(res==WAIT_FAILED) break ;
		elock.lock() ;
 		size_t m = EmptyIndices.size();
		if (n < m) {
			if (ModerateAllocating&&m>EmptyLimit)
				result = true;
			n = m;
		}
		if (n > EmptyBorder) result = true ;
        if(result) break ;
	  }while(!Terminated) ;

    DBGOUT("Async FIFO allocation: allocation waiting %s.\r\n",result?"completed":"failed") ;

	return result ;
}
//---------------------------------------------------------------------------
} // End of namespace PRY8EAlByw
//===========================================================================
