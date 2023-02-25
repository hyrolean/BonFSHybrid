//===========================================================================
#include "stdafx.h"
#include <cctype>
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
// acalci
//---------------------------------------------------------------------------

    template<typename T>
    class integer_c_expression_const_table 
    {
    protected:
      typedef map<string,T> const_map_t;
      const_map_t const_map ;
    public:
      void clear() { const_map.clear(); }
      void entry(const string &name, const T val) { const_map[name]=val; }
      bool find(const string &name, T &val) const {
        const_map_t::const_iterator pos = const_map.find(name);
        if(pos==const_map.end()) return false;
        val=pos->second ;
        return true;
      }
    };

    template<typename T>
    class integer_c_expression_string_calculator
    {
    protected:
      enum token_t {
        tEND    = 0,
        tSTART  = 1,
        tVAL    = 10,
        tPLUS   = 20,  /* prec ADD */
        tMINUS  = 21,  /* prec SUB */
        tNOT    = 22,
        tFACT   = 30,
        tMUL    = 40,
        tDIV    = 41,
        tMOD    = 42,
        tADD    = 50,
        tSUB    = 51,
        tLSHIFT = 60,
        tRSHIFT = 61,
        tAND    = 70,
        tXOR    = 80,
        tOR     = 90,
        tLP     = 100,
        tRP     = 101,
      };
      struct node_t {
        token_t token ;
        T val ;
        struct node_t *next ;
        node_t(): token(tEND),val(0),next(NULL) {}
      };
      node_t *top ;
      T def_val ;
      integer_c_expression_const_table<T> *const_table ;
      bool halfway ;
      const char *es ;
    protected:
      static T strimmval(const char *s,char **endptr,int radix) {
        #if 1
        T val=0;
        while(*s) {
          int c = toupper(*s) ; T v ;
          if(c>='0'&&c<='9'&&c-'0'<radix)
            v = c-'0' ;
          else if(radix>10&&c>='A'&&c<='Z'&&c-'A'<radix-10)
            v = c-'A'+10 ;
          else break ;
          switch(radix) {
            case 2:  val<<=1; break;
            case 8:  val<<=3; break;
            case 16: val<<=4; break;
            default: val*=radix;
          }
          val += v ;
          s++;
        }
        if(endptr) *endptr=const_cast<char*>(s) ;
        return val ;
        #else
        if(sizeof(T)>sizeof(long))
          return static_cast<T>(strtoull(s,endptr,radix)); // NG: @VS2008
        return static_cast<T>(strtoul(s,endptr,radix));
        #endif
      }
      static void skip_separator(const char *&s) {
        while(*s==' '||*s=='\t'||*s=='\r'||*s=='\n') s++;
      }
      static void skip_val_literal(const char *&s) {
        while(*s&&(isalnum(*s)||*s=='_')) s++;
      }
      T __fastcall parse(node_t *backTK) {
        node_t nextTK;
        backTK->next = &nextTK;
        skip_separator(es);
        if(*es>='0'&&*es<='9') {
          char *e=NULL;
          nextTK.token=tVAL;
          if(es[0]=='0') {
            if(es[1]=='b'||es[1]=='B') // bin
              nextTK.val=strimmval(es+=2,&e,2);
            else if(es[1]=='x'||es[1]=='X') // hex
              nextTK.val=strimmval(es+=2,&e,16);
            else // oct
              nextTK.val=strimmval(es++,&e,8);
          }else // dec
            nextTK.val=strimmval(es++,&e,10);
          if(e) es=e;
          switch(*es) { // binary units
            case 'K': nextTK.val<<=10, es++; break ; // Ki
            case 'M': nextTK.val<<=20, es++; break ; // Mi
            case 'G': nextTK.val<<=30, es++; break ; // Gi
            case 'T': nextTK.val<<=40, es++; break ; // Ti (over int)
            case 'P': nextTK.val<<=50, es++; break ; // Pi
            case 'E': 
            case 'X': nextTK.val<<=60, es++; break ; // Ei
            case 'Z': nextTK.val<<=70, es++; break ; // Zi (over __int64)
            case 'Y': nextTK.val<<=80, es++; break ; // Yi 
          }
          skip_val_literal(es) ;
        }
        else if(!strncmp("**",es,2)) nextTK.token=tFACT,   es+=2; 
        else if(!strncmp("<<",es,2)) nextTK.token=tLSHIFT, es+=2; 
        else if(!strncmp(">>",es,2)) nextTK.token=tRSHIFT, es+=2; 
        else {
          switch(*es) {
            case '+': nextTK.token=tADD; break;
            case '-': nextTK.token=tSUB; break;
            case '~': nextTK.token=tNOT; break;
            case '*': nextTK.token=tMUL; break;
            case '/': nextTK.token=tDIV; break;
            case '%': nextTK.token=tMOD; break;
            case '&': nextTK.token=tAND; break;
            case '^': nextTK.token=tXOR; break;
            case '|': nextTK.token=tOR;  break;
            case '(': nextTK.token=tLP;  break;
            case ')': nextTK.token=tRP;  break;
            case ';': es++;
            case '\0':
              return calculate(); // terminal
            default:
              if(const_table&&(isalpha(*es)||*es=='_')) { // constant
                const char *e = es ;
                skip_val_literal(e) ;
                if(const_table->find(string(es,e-es),nextTK.val)) {
                  nextTK.token=tVAL;
                  es=e-1; 
                  break;
                }
              }
              return calculate(); // outside of c-expression literals
          }
          es++;
        }
        return parse(&nextTK); // parse the next token
      }
      T calculate() {
        int num=0;
        node_t *p=top->next;
        while(p&&p->token!=tEND) {
          num++;
          p=p->next;
        }
        return do_calculate(num,top->next);
      }
      static node_t* nest_node(node_t *p,int num) {
        while(num>0) p=p->next, num--;
        return p;
      }
      T __fastcall do_calculate(int &num,node_t *pos)
      {
      #define D1(a)  (p->token==(a))
      #define D2(a,b)  (D1(a)&&p->next->token==(b))
      #define D3(a,b,c) (D2(a,b)&&p->next->next->token==(c))
        if(!pos) {num=0; return def_val;}
        //PRIOR100: ( )  <- digest parenthesses at first
        node_t *p=pos;
        for(int i=0;i<num-1;i++,p=p->next) {
          if(D1(tRP)) break ;
          if(D1(tLP)) {
            num -= i+1;
            do_calculate(num,p->next);
            num += i+1;
            if(num-i>=3&&D3(tLP,tVAL,tRP)) {
              p->val= ( p->next->val );
              p->token=tVAL;
              p->next=nest_node(p,3);
              num-=2;
            }
          }
        }
        //PRIOR(20-90): digest operators ( parenthesses excluded )
        node_t *q;
        int n;
        //PRIOR20: + - ~ (single)
        do {
          p=pos, q=NULL, n=0 ;
          for (int i = 0; i < num - 1; q = p, p = p->next, i++) {
            if(D1(tRP)) break ;
            if(q&&q->token==tVAL) // NG: the LHS value is existed
              continue;
            if(D2(tADD,tVAL))       p->val= + p->next->val;
            else if(D2(tSUB,tVAL))  p->val= - p->next->val;
            else if(D2(tNOT,tVAL))  p->val= ~ p->next->val;
            else continue ;
            p->token=tVAL;
            p->next=nest_node(p,2);
            num--;
            if(q&&(q->token==tADD||q->token==tSUB||q->token==tNOT))
              n++;
          }
        }while(n>0);
        //PRIOR30: **
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tFACT,tVAL)) {
            T val=p->val;
            q=p->next->next;
            if(q->val==0)
              p->val=1;
            else if(q->val<0)
              p->val=0;
            else while(q->val-1) {
              p->val*=val;
              q->val--;
            }
            p->next=q->next;
            num-=2;
          }
          else i++, p=p->next;
        }
        //PRIOR40: * / %
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tMUL,tVAL))        p->val=p->val * p->next->next->val;
          else if(D3(tVAL,tDIV,tVAL))   p->val=p->val / p->next->next->val;
          else if(D3(tVAL,tMOD,tVAL))   p->val=p->val % p->next->next->val;
          else { i++;p=p->next; continue; }
          p->next=nest_node(p,3);
          num-=2;
        }
        //PRIOR50: + -
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tADD,tVAL))        p->val=p->val + p->next->next->val;
          else if(D3(tVAL,tSUB,tVAL))   p->val=p->val - p->next->next->val;
          else { i++;p=p->next; continue; }
          p->next=nest_node(p,3);
          num-=2;
        }
        //PRIOR60: << >>
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tLSHIFT,tVAL))      p->val=p->val << p->next->next->val;
          else if(D3(tVAL,tRSHIFT,tVAL)) p->val=p->val >> p->next->next->val;
          else { i++;p=p->next; continue; }
          p->next=nest_node(p,3);
          num-=2;
        }
        //PRIOR70: &
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tAND,tVAL)) {
            p->val=p->val & p->next->next->val;
            p->next=nest_node(p,3);
            num-=2;
          }
          else i++, p=p->next;
        }
        //PRIOR80: ^
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tXOR,tVAL)) {
            p->val=p->val ^ p->next->next->val;
            p->next=nest_node(p,3);
            num-=2;
          }
          else i++, p=p->next;
        }
        //PRIOR90: |
        p=pos;
        for(int i=0;i<num-2;) {
          if(D1(tRP)) break ;
          if(D3(tVAL,tOR,tVAL)) {
            p->val=p->val | p->next->next->val;
            p->next=nest_node(p,3);
            num-=2;
          }
          else i++, p=p->next;
        }
        //PRIOR10: a VAL token is finally existed at the head or not
        return pos&&pos->token==tVAL&&(halfway||num==1)? pos->val: def_val;
      #undef D1
      #undef D2
      #undef D3
      }
    public:
      T execute(
            const char *expression_string, T default_value,
            const char **out_end_pointer=NULL,
            integer_c_expression_const_table<T> *constant_var_table=NULL,
            bool allow_indigestion=false ) {
        node_t sTK ;
        sTK.token = tSTART ;
        top = &sTK ;
        es = expression_string ;
        def_val = default_value ;
        halfway = allow_indigestion ;
        const_table = constant_var_table ;
        T ret = es ? parse(top) : def_val ;
        if(out_end_pointer) *out_end_pointer = es ;
        return ret ;
      }
    };
    

//----- acalci entity -----------------------------------------------
/* Calculate the c-expression string and convert it to an integer. */
/********************************************************************

  int acalci(const char *s, int defVal, const char **endPtr, bool allowIndigest)

    s: A c-expression string consisted of operators and terms.
    defVal: A returned value as default on being failed to calculate.
    endPtr: An output value wherence end of c-expression literals on s.
    allowIndigest: Allow the indigestion of operators or not.
    [result]: A value as converted result of calculating s.

    Operators and Associativity::
    (The order of priority is from top to bottom.)
    ==============================================
        ( )                    -> left to right
        + - ~ (single)         <- right to left
        ** (factorial)         -> left to right
        * / %                  -> left to right
        + -                    -> left to right
        << >>                  -> left to right
        &                      -> left to right
        ^                      -> left to right
        |                      -> left to right
    ==============================================
    Operator-meanings are almost same as C-Lang.

    Only integers can be terms as the imm value.
    Integer formats:: (described below as regexp.)
    ==============================================
      0[bB][01]+                  Binary digits
      [1-9][0-9].                 Decimal digits
      0[0-7].                     Octal digits
      0[xX][0-9a-fA-F]+           Hexical digits
    ==============================================

    Separator:: [SPACE][TAB(\t)][CR(\r)][LF(\n)]

********************************************************************/

  static integer_c_expression_const_table<int> acalci_const_table ;
  static integer_c_expression_const_table<__int64> acalci64_const_table ;

//-----
int acalci(const char *s, int defVal, const char **endPtr, bool allowIndigest)
{
  integer_c_expression_string_calculator<int> calculator ;
  return calculator.execute(s, defVal, endPtr, &acalci_const_table, allowIndigest) ;
}
//-----
__int64 acalci64(const char *s, __int64 defVal, const char **endPtr, bool allowIndigest)
{
  integer_c_expression_string_calculator<__int64> calculator ;
  return calculator.execute(s, defVal, endPtr, &acalci64_const_table, allowIndigest) ;
}
//-----
void acalci_entry_const(const char *name, int val)
{
  if(!name) acalci_const_table.clear();
  else acalci_const_table.entry(name, val);
}
//-----
void acalci64_entry_const(const char *name, __int64 val)
{
  if(!name) acalci64_const_table.clear();
  else acalci64_const_table.entry(name, val);
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
