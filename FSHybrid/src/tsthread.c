/* fsusb2i   (c) 2015-2016 trinity19683
  TS USB I/O thread (MS-Windows)
  tsthread.c
  2016-02-18
*/

#include "stdafx.h"
#include <errno.h>
#include <string.h>
#include <process.h>
#include <WinUSB.h>
#include <assert.h>

#include "usbops.h"
#include "osdepend.h"
#include "tsbuff.h"
#include "tsthread.h"

#if defined(_WIN32) && !defined(_WIN64)
//#define X86_ASM
#endif

#define HasSignal(e) (WaitForSingleObject(e,0)==WAIT_OBJECT_0)

#define ROUNDUP(n,w) (((n) + (w)) & ~(unsigned)(w))

//# pipe policy settings
BOOL USBPIPEPOLICY_RAW_IO = TRUE ;
BOOL USBPIPEPOLICY_AUTO_CLEAR_STALL = TRUE ;
BOOL USBPIPEPOLICY_ALLOW_PARTIAL_READS = TRUE ;
BOOL USBPIPEPOLICY_AUTO_FLUSH = FALSE ;
BOOL USBPIPEPOLICY_IGNORE_SHORT_PACKETS = FALSE ;
BOOL USBPIPEPOLICY_SHORT_PACKET_TERMINATE = FALSE ;
DWORD USBPIPEPOLICY_PIPE_TRANSFER_TIMEOUT = 5000UL ;
BOOL USBPIPEPOLICY_RESET_PIPE_ON_RESUME = FALSE ;


struct TSIO_CONTEXT {
#ifdef INCLUDE_ISOCH_XFER
	USBD_ISO_PACKET_DESCRIPTOR isochFrameDesc[ISOCH_PacketFrames];
#endif
	OVERLAPPED ol;
	int index;
	DWORD bytesRead;
};

struct tsthread_param {
	HANDLE hThread;    //# handle to thread data
	unsigned char volatile  flags;
	/* if 0x01 flagged, issue a new request.
	   if 0x02 flagged, cancel requests and stop thread.
	*/
	const struct usb_endpoint_st*  pUSB;
	char* buffer;    //# data buffer (in heap memory)
	int*  actual_length;    //# actual length of each buffer block
	unsigned xfer_size;
	unsigned buff_size;
	unsigned buff_unitSize;
	int buff_num;
	int buff_push;
	int buff_pop;
	int total_submit ;
	struct TSIO_CONTEXT ioContext[TS_MaxNumIO];
	HANDLE hTsEvents[TS_MaxNumIO*2-1] ;
	HANDLE hTsAvailable,hTsRead,hTsRestart,hTsStopped ;

	struct tsfifo_t* tsfifo ; //# ts fifo caching object

	//# critical object
	CRITICAL_SECTION csTsExclusive;

#ifdef INCLUDE_ISOCH_XFER
	WINUSB_ISOCH_BUFFER_HANDLE hIsochBuffer;
#endif
};

  static int __inline isCritical(struct tsthread_param* const ps) {
	return !(ps->flags & 0x01)||HasSignal(ps->hTsRestart) ;
  }

  static void __inline lockWinUsb(struct tsthread_param* const ps,int lock) {
	if(ps->pUSB->lockunlockFunc)
		 ps->pUSB->lockunlockFunc(ps->pUSB->dev,lock) ;
  }


static void tsthread_purgeURB(const tsthread_ptr ptr)
{
	struct tsthread_param* const ps = ptr;
	int i;

	EnterCriticalSection(&ps->csTsExclusive);
	lockWinUsb(ps,1);

	if(!(ps->pUSB->endpoint & 0x100) ) { //# Bulk

		WinUsb_AbortPipe(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF);

		if(ps->total_submit>0) {

			for (i = 0;i < TS_MaxNumIO;i++) {
				struct TSIO_CONTEXT* pContext = &ps->ioContext[i];
				if(pContext->index>=0) {
					if(ps->tsfifo&&ps->tsfifo->writeBackFinish)
						ps->tsfifo->writeBackFinish(pContext->index,0,ps->tsfifo->arg);
					/*
					else
						ps->actual_length[pContext->index]=0;*/
					ResetEvent(pContext->ol.hEvent);
					pContext->index=-1 ;
				}
			}


			ps->total_submit = 0;

		}
	}


	#if 1
	if(ps->tsfifo) {
		if(ps->tsfifo->purge)
			ps->tsfifo->purge(ps->tsfifo->arg);
	}
	if(ps->actual_length) {
		if(!(ps->pUSB->endpoint & 0x100) ) { //# Bulk
			for (i = 0;i < ps->buff_num;i++) {
				ps->actual_length[i]=-1 ;
			}
			ps->buff_pop = ps->buff_push ;
		}
        else { //# Isochronous
			void* ptr; while(tsthread_read( ps, &ptr )>0);
		}
	}
	#endif

	WinUsb_FlushPipe(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF);
	ps->flags &= ~0x10; //# reset isochronous continuous status

	lockWinUsb(ps,0);
	LeaveCriticalSection(&ps->csTsExclusive);
}

//# 2020-10-5
//#   Added the URB feature for the isochronous transfer mode.
//# 2018-3-1
//#   Removed the tsthread_submitURB static function.
//#   Removed the tsthread_reapURB static function.
//#   Added the tsthread_bulkURB static function instead of submitURB/reapURB.
//# Fixed by 2018-2020 LVhJPic0JSk5LiQ1ITskKVk9UGBg
static unsigned int tsthread_bulkURB(struct tsthread_param* const ps)
{

	//# number of the prefetching buffer busy area that shouldn't be submitted
	const int POPDELTA = (TS_CalcDeadZone(ps->buff_size)+ps->buff_unitSize-1)/ps->buff_unitSize;

	int ri=0; //# the circular index based ioContext cursor for reaping
	int si=0; //# the circular index based ioContext cursor for submitting
	int i ;
	BOOL bRet ;
	DWORD dRet=0 ;

	//# the pointer of a ts fifo caching object
	struct tsfifo_t *pTSFifo = ps->tsfifo ;

	//# the write through ts fifo caching feature is existed or not
	const BOOL hasWThrough = pTSFifo && pTSFifo->writeThrough ? TRUE : FALSE ;

	//# the write back ts fifo caching feature is enabled or not
	const BOOL isWBack =
#ifdef INCLUDE_ISOCH_XFER
			(ps->pUSB->endpoint & 0x100) ? FALSE:
#endif
			(pTSFifo && pTSFifo->writeBackBegin && pTSFifo->writeBackFinish ? TRUE: FALSE);

	//# prefetching frames per a packet
	const int frames =
#ifdef INCLUDE_ISOCH_XFER
		(ps->pUSB->endpoint & 0x100) ? ISOCH_PacketFrames :
#endif
		1 ;

	//# check the contradiction of caching methods
	assert( !isWBack != (ps->actual_length==NULL) ) ;
	assert( !isWBack || (isWBack && !hasWThrough) ) ;

	//# bulk loop
	while(!(ps->flags&0x02)) {

		if(HasSignal(ps->hTsRestart)) {
			tsthread_purgeURB(ps) ;
			ResetEvent(ps->hTsRestart) ;
			continue;
		}

		if (!(ps->flags & 0x01)) {
			SetEvent(ps->hTsStopped);
			WaitForSingleObject(ps->hTsAvailable, TS_PollTimeout) ;
			continue ;
		}

#ifndef INCLUDE_ISOCH_XFER
		if (ps->pUSB->endpoint & 0x100) { //# Isochronous
			if(ps->flags & 0x01) {
				tsthread_stop(ps);
				continue ;
			}
		}
#endif

		if(ps->total_submit>0) {

			//# poll
			int next_wait_index=-1 ;
			int max_wait_count = ps->total_submit<MAXIMUM_WAIT_OBJECTS ? ps->total_submit : MAXIMUM_WAIT_OBJECTS ;
			dRet = WaitForMultipleObjects(max_wait_count, &ps->hTsEvents[ri] , FALSE, TS_PollTimeout );
			if (isCritical(ps)) continue;
			if(WAIT_OBJECT_0 <= dRet&&dRet < WAIT_OBJECT_0+max_wait_count) {
				int end_index=(ri+ps->total_submit)%TS_MaxNumIO ;
				next_wait_index = ((dRet - WAIT_OBJECT_0)+1 + ri)%TS_MaxNumIO ;
				while(next_wait_index!=end_index) {
					if(!HasSignal(ps->hTsEvents[next_wait_index]))
						break ;
					if (++next_wait_index >= TS_MaxNumIO)
						next_wait_index ^= next_wait_index ;
				}
			}else if(WAIT_TIMEOUT!=dRet) {
				dRet = GetLastError();
				warn_info(dRet,"poll failed");
				break;
			}

			//# reap
			if(next_wait_index>=0) do {

				struct TSIO_CONTEXT* pContext = &ps->ioContext[ri];
				if(pContext->index>=0) {

					DWORD bytesRead=pContext->bytesRead ;

					if (isCritical(ps)) break;
					//if(!HasSignal(ps->hTsEvents[ri])) break ;
					if(bytesRead>0) {
						bRet = TRUE;
						dRet = 0;
					}else {
						lockWinUsb(ps,1) ;
						if (isCritical(ps)) {
							bRet=FALSE ;
							dRet=ERROR_OPERATION_ABORTED ;
						}else {
							bRet = WinUsb_GetOverlappedResult(
								ps->pUSB->fd, &(pContext->ol), &bytesRead, FALSE);
							dRet = GetLastError();
						}
						lockWinUsb(ps,0) ;
					}
					if (ps->buff_unitSize < bytesRead) {
						DBGOUT("reap: size over (size=%d)\n",bytesRead) ;
						warn_info(bytesRead, "reapURB overflow");
						bytesRead = ps->buff_unitSize;
					}
					if(bRet) {
#ifndef INCLUDE_ISOCH_XFER
						if (ps->pUSB->endpoint & 0x100)
							bytesRead = 0;
#endif
#ifdef TS_IgnoreShortPacket
						if (ps->xfer_size>bytesRead) {
							//if(bytesRead>0)
								DBGOUT("reap: short packet (size=%d)\n",bytesRead) ;
							bytesRead=0;
						}
#endif
					}else {
						DBGOUT("reap: error (code=%d, size=%d)\n",dRet,bytesRead) ;
						if(ERROR_IO_INCOMPLETE == dRet) { //# incomplete
							break;  //# looking forward to the next time...
						}
						#if 1
						if(ERROR_SEM_TIMEOUT == dRet) { //# timeout
							break;  //# looking forward to the next time...
						}
						#endif
						//# failed
						bytesRead = 0;
						//ps->flags &= ~0x10;
						warn_msg(dRet, "reapURB%u failed", ri);
					}
					EnterCriticalSection(&ps->csTsExclusive) ;
					if(pContext->index>=0) {
						if (ps->pUSB->endpoint & 0x100) {
#ifdef INCLUDE_ISOCH_XFER
#if defined(_WIN32) && !defined(_WIN64)
							
							int stride = (char*) &pContext->isochFrameDesc[1]
								- (char*) &pContext->isochFrameDesc[0] ;
							void* sp_ = &pContext->isochFrameDesc[0].Length ;
							void* dp_ = &ps->actual_length[pContext->index] ;
							int dx_ = (char*) &pContext->isochFrameDesc[0].Status
								- (char*) &pContext->isochFrameDesc[0].Length ;
							int errorCnt = 0 ;
							_asm {
								mov ecx, frames
								mov esi, sp_
								mov edi, dp_
								mov edx, dx_
								mov eax, stride
								cld
								cmp bRet, 0
								je lb3
								xor ebx, ebx
							lb1:
								cmp dword ptr [esi+edx], 0
								jne lb2
								movsd
								dec ecx
								jz lb4
								lea esi, [esi+eax-4]
								jmp lb1
							lb2:
								mov dword ptr [edi], 0
								lea edi, [edi+4]
								lea esi, [esi+eax]
								inc ebx
								dec ecx
								jnz lb1
								jmp lb4
							lb3:
								mov ebx, ecx
								xor eax, eax
								rep stosd
							lb4:
								mov errorCnt, ebx
							}
							if(errorCnt>0)
								warn_msg(dRet, "reapURB(%u)", errorCnt);
#else
							for (i = 0; i < frames; i++) {
								int idx = pContext->index+i ;
								if (!bRet||pContext->isochFrameDesc[i].Status) {
									ps->actual_length[idx] =  0;
									warn_msg(dRet, "reapURB%u.%u", ri, i);
								}
								else {
									DWORD ln = pContext->isochFrameDesc[i].Length ;
									ps->actual_length[idx] = ln ;
									//dmsgn("reapURB%u.%u=%d, ", ri, i, br);
								}
							}
#endif
#endif
						}else {
							if (isWBack)
								pTSFifo->writeBackFinish(ri, bytesRead, pTSFifo->arg);
							else
								ps->actual_length[pContext->index] = bytesRead;
							if(bytesRead) SetEvent(ps->hTsAvailable) ;
						}
						ResetEvent(ps->hTsEvents[ri]);
						pContext->index=-1 ;
						ps->total_submit-- ;
					}
					LeaveCriticalSection(&ps->csTsExclusive) ;
				}
				if(++ri>=TS_MaxNumIO) ri^=ri ;
				if(ps->total_submit<=0) break ;

			}while(ri!=next_wait_index);

		}

		if(!ps->total_submit) {

			//# I/O stall
			ri = si ;
			DBGOUT("io stall\n");

		}

		if(hasWThrough) {

			//# write through caching
			BYTE *esi=NULL,*edi ;
			int sz,amount=0;
			do {
				edi = NULL ;
				sz  = tsthread_read( ps, (void**) &edi ) ;
				if(edi) {
                    if(!esi)
						esi=edi, amount=sz, sz=0 ;
                    else if(&esi[amount]==edi)
                        amount+=sz, sz=0 ;
				}
                if(!edi||sz>0) {
                    if(esi) pTSFifo->writeThrough(esi, amount, pTSFifo->arg) ;
                    esi=edi, amount=sz ;
                }
			}while(esi);

		}

		if (HasSignal(ps->hTsRestart)) continue;

		if(ps->total_submit<TS_MaxNumIO) {

			//# submit
			if(ps->flags & 0x01) {
				void *buffer;
				DWORD lnTransfered;
				int num_empties=0,max_empties=TS_MaxNumIO;
				int last_state=0;
				dRet = 0;bRet = FALSE;
				//# calculate the real maximum number of submittable empties
				if(!isWBack) {
					EnterCriticalSection(&ps->csTsExclusive) ;
					tsthread_readable(ps);
					last_state = ps->actual_length[ps->buff_pop] ;
					#if 1
					if(ps->buff_push==ps->buff_pop)
						max_empties =
							last_state>0||last_state==-2 ? 0 : ps->buff_num ;
					else
					#endif
						max_empties = ps->buff_push<ps->buff_pop ?
							ps->buff_pop-ps->buff_push :
							ps->buff_num-ps->buff_push + ps->buff_pop ;
					ResetEvent(ps->hTsRead);
					LeaveCriticalSection(&ps->csTsExclusive) ;
					max_empties -= POPDELTA; //# subtract deadzone
				}
				//# summary amount of empties
				num_empties=TS_MaxNumIO-ps->total_submit;
				num_empties*=frames;
				//start=si ;
				if(num_empties>max_empties) {
					num_empties = max_empties ;
					#if 1
					if(num_empties<=0&&ps->total_submit<=0) {
						//# in the dead zone
						if(!isWBack&&last_state>0) {
							HANDLE events[2];
							events[0]=ps->hTsRead;
							events[1]=ps->hTsRestart ;
							//# wait for reading buffer...
							dRet=WaitForMultipleObjects(2, events , FALSE, TS_PollTimeout );
							if(dRet==WAIT_TIMEOUT)
							  DBGOUT("read: wait timeout\n") ;
							else if(dRet==WAIT_OBJECT_0)
							  DBGOUT("read: wait success\n") ;
						}
					}
					#endif
				}
				//# submit to empties
				while (num_empties-frames >= 0) {
					struct TSIO_CONTEXT* pContext = &ps->ioContext[si];
					if (isCritical(ps)) break;
					if(ps->total_submit>=TS_MaxNumIO) break;
					if (pContext->index>=0) break ;
					//if (HasSignal(ps->hTsEvents[ri])) break;
#ifndef INCLUDE_ISOCH_XFER
					if (ps->pUSB->endpoint & 0x100) { //# Isochronous
						//tsthread_stop(ps);
						break;
					}
#endif
					if(isWBack) {
						buffer = pTSFifo->writeBackBegin(si, ps->buff_unitSize, pTSFifo->arg) ;
						if(!buffer) {
							break ; //# buffer overflow
						}
					}
					else {
						BOOL busy=FALSE ;
						for(i=0;i<frames&&!busy;i++) {
							if(ps->actual_length[ps->buff_push+i]>0||ps->actual_length[ps->buff_push+i]==-2)
								busy=TRUE ; //# buffer busy
						}
						if(busy) break ;
					}
					EnterCriticalSection(&ps->csTsExclusive) ;
					if(!isWBack) {
						buffer = ps->buffer + (ps->buff_push * ps->buff_unitSize) ;
						last_state =  ps->actual_length[ps->buff_push] ;
						ps->actual_length[ps->buff_push] = -2;
					}
					pContext->index = isWBack ? si : ps->buff_push;
					pContext->bytesRead = 0 ;
					ZeroMemory(&pContext->ol,sizeof(OVERLAPPED));
					pContext->ol.hEvent = ps->hTsEvents[si];
					lnTransfered = 0;
					ResetEvent(pContext->ol.hEvent);
					lockWinUsb(ps,1) ;
					if(isCritical(ps)) {
						bRet=FALSE ;
						dRet=ERROR_OPERATION_ABORTED ;
					}else{
						if (ps->pUSB->endpoint & 0x100) { //# Isochronous
#ifdef INCLUDE_ISOCH_XFER
							bRet = WinUsb_ReadIsochPipeAsap(
								ps->hIsochBuffer,
								ps->buff_push * ps->buff_unitSize, // offset
								frames * ps->buff_unitSize,  // length
								ps->flags & 0x10 ? TRUE: FALSE, // continuous
								frames, pContext->isochFrameDesc,
								&(pContext->ol));
							dRet = GetLastError();
							if (!bRet && ERROR_INVALID_PARAMETER == dRet)
								ps->flags &= ~0x10;
							else if(!(ps->flags & 0x10))
								ps->flags |= 0x10;
#endif
						}else {
							bRet = WinUsb_ReadPipe(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF,
								buffer, ps->buff_unitSize, &lnTransfered, &(pContext->ol));
							dRet = GetLastError();
						}
					}
					lockWinUsb(ps,0) ;
					if (FALSE == bRet && ERROR_IO_PENDING != dRet) {
						//# submitting failed
						DBGOUT("submit: error (code=%d, size=%d)\n",dRet,lnTransfered) ;
						warn_info(dRet, "submitURB failed");
						if(isWBack)
							pTSFifo->writeBackFinish(si,0,pTSFifo->arg);
						else
							ps->actual_length[ps->buff_push] = last_state;
						ResetEvent(ps->hTsEvents[si]);
						pContext->index = -1;
					}else {
						//# submitting succeeded
						if(!isWBack) {
#if defined( _WIN32 ) && !defined( _WIN64 ) 
							if(frames>1) {
								void* dp_ = &ps->actual_length[ps->buff_push+1];
								_asm {
									mov edi, dp_
									mov eax, -2
									mov ecx, frames
									dec ecx
									cld
									rep stosd
								}
							}
#else
						    for(i=1;i<frames;i++)
								ps->actual_length[ps->buff_push+i] = -2;
#endif
							if(ps->buff_push+frames>=ps->buff_num)
								ps->buff_push=0;
							else
								ps->buff_push+=frames;
						}
						if(bRet) {
							DBGOUT("submit: success (size=%d)\n",lnTransfered) ;
							pContext->bytesRead = lnTransfered ;
							SetEvent(ps->hTsEvents[si]) ;
						}else {
							if(lnTransfered>0)
								DBGOUT("submit: pending (size=%d)\n",lnTransfered) ;
						}
						dRet = 0;
						if(++si >= TS_MaxNumIO) si^=si;
						++ps->total_submit;
					}
					LeaveCriticalSection(&ps->csTsExclusive) ;
					//# check submitting failed or not
					if(/*dRet||*/pContext->index<0) break ;
					num_empties-=frames;
				}
				//if(dRet) break ;
			}

		}

	}

	//# dispose
	tsthread_purgeURB(ps);

	return dRet ;
}

/* TS thread function issues URB requests. */
static unsigned int __stdcall tsthread(void* const param)
{
	struct tsthread_param* const ps = param;
	unsigned int result = 0;

	ps->buff_push = 0;

	result = tsthread_bulkURB(ps);

	_endthreadex( 0 );
	return result ;
}

/* public function */

int tsthread_create( tsthread_ptr* const tptr,
					 const struct usb_endpoint_st* const pusbep,
					 const struct tsfifo_t * const ptsfifo
				   )
{

	struct tsthread_param* ps;
	DWORD dwRet, i;

	{ //#
		const BOOL tsfifo_exists = ptsfifo ? TRUE : FALSE ;
		const BOOL wback_exists =
#ifdef INCLUDE_ISOCH_XFER
			(pusbep->endpoint & 0x100) ? FALSE:
#endif
			(tsfifo_exists && ptsfifo->writeBackBegin && ptsfifo->writeBackFinish? TRUE: FALSE);
		const unsigned param_size = ROUNDUP( sizeof( struct tsthread_param ), 0xF );
		const unsigned xferSize = pusbep->xfer_size ;
		const unsigned unitSize = pusbep->endpoint & 0x100 ? xferSize : ROUNDUP( xferSize, 0x1FF ) ;
		const unsigned unitNum =
#ifdef INCLUDE_ISOCH_XFER
			pusbep->endpoint & 0x100 ? ISOCH_PacketFrames*TS_BufPackets :
#endif
			TS_CalcBufSize(xferSize)/unitSize ;
		const unsigned buffSize = wback_exists ? 0 : unitSize*unitNum ;
		const unsigned buffer_size = wback_exists ? 0 : ROUNDUP( buffSize+TS_DeltaSize , 0xF );
		const unsigned actlen_size = wback_exists ? 0 : sizeof( int ) * unitNum;
		const unsigned tsfifo_size = tsfifo_exists ? ROUNDUP( sizeof( struct tsfifo_t ), 0xF) : 0;
		char *ptr, *buffer_ptr;
		struct tsfifo_t *tsfifo_ptr;
		unsigned totalSize = param_size + actlen_size + buffer_size + tsfifo_size ;
		ptr = uHeapAlloc( totalSize );
		if ( NULL == ptr ) {
			dwRet = GetLastError();
			warn_msg( dwRet, "failed to allocate TS buffer" );
			return -1;
		}
		buffer_ptr = ptr;
		ptr += buffer_size;
		ps = ( struct tsthread_param* ) ptr;
		ps->buffer = buffer_ptr;
		ptr += param_size;
		ps->actual_length = ( int* ) ptr ;
		ptr += actlen_size;
		tsfifo_ptr = ( struct tsfifo_t * ) ptr;
		ptr += tsfifo_size;
		ps->xfer_size= xferSize;
		ps->buff_unitSize = unitSize;
		ps->buff_num = unitNum;
		ps->buff_size = buffSize;
		assert(ptr-buffer_ptr==totalSize) ;
		if ( actlen_size ) {
			for ( i = 0;i < unitNum;i++ )
				ps->actual_length[ i ] = -1;   //# reset all values to empty
		}else
			ps->actual_length = NULL;
		if ( tsfifo_exists ) {
			CopyMemory( tsfifo_ptr, ptsfifo, sizeof( struct tsfifo_t ) );
			ps->tsfifo = tsfifo_ptr;
		}else
			ps->tsfifo = NULL ;
	}
	ps->pUSB = pusbep;
	ps->flags = 0;
	ps->buff_pop = 0;
	ps->total_submit = 0;
#ifdef INCLUDE_ISOCH_XFER
	ps->hIsochBuffer = NULL;
	if (pusbep->endpoint & 0x100)
		DBGOUT("-*- ISOCHRONOUS TRANSFER MODE -*-\n") ;
#else
	if (pusbep->endpoint & 0x100) { //# Isochronous
		warn_msg(0, "Please change to BULK transfer mode :-P");
	}
#endif

	for ( i = 0; i < TS_MaxNumIO; i++ ) {
		ps->ioContext[ i ].index = -1;    //# mark it unused
		ps->hTsEvents[ i ] = CreateEvent( NULL, TRUE, FALSE, NULL );
		ZeroMemory( &ps->ioContext[ i ].ol, sizeof( OVERLAPPED ) );
		ps->ioContext[ i ].ol.hEvent = ps->hTsEvents[ i ];
	}

	//# it arranges for event handles to look like circular buffer
	for ( i = 0; i < TS_MaxNumIO - 1; i++ ) {
		ps->hTsEvents[ i + TS_MaxNumIO ] = ps->hTsEvents[ i ];
	}
	ps->hTsAvailable = CreateEvent( NULL, FALSE, FALSE, NULL );
	ps->hTsRead = CreateEvent( NULL, FALSE, FALSE, NULL );
	ps->hTsRestart = CreateEvent( NULL, TRUE, FALSE, NULL );
	ps->hTsStopped = CreateEvent( NULL, FALSE, FALSE, NULL );
	InitializeCriticalSection(&ps->csTsExclusive) ;

	//# USB endpoint
	WinUsb_ResetPipe( pusbep->fd, pusbep->endpoint & 0xFF );
	#define SETUSBPIPEPOLICY_BOOL(name) do { UCHAR v=(DWORD)USBPIPEPOLICY_##name?1:0; \
		WinUsb_SetPipePolicy( pusbep->fd, \
			pusbep->endpoint & 0xFF, name,  sizeof( UCHAR ), &v ); }while(0)
	#define SETUSBPIPEPOLICY_DWORD(name) do { DWORD v=(DWORD)USBPIPEPOLICY_##name; \
		WinUsb_SetPipePolicy( pusbep->fd, \
			pusbep->endpoint & 0xFF, name,  sizeof( DWORD ), &v ); }while(0)
	SETUSBPIPEPOLICY_BOOL(RAW_IO);
	SETUSBPIPEPOLICY_BOOL(AUTO_CLEAR_STALL);
	SETUSBPIPEPOLICY_BOOL(ALLOW_PARTIAL_READS);
	SETUSBPIPEPOLICY_BOOL(AUTO_FLUSH);
	SETUSBPIPEPOLICY_BOOL(IGNORE_SHORT_PACKETS);
	SETUSBPIPEPOLICY_BOOL(SHORT_PACKET_TERMINATE);
	SETUSBPIPEPOLICY_DWORD(PIPE_TRANSFER_TIMEOUT);
	SETUSBPIPEPOLICY_BOOL(RESET_PIPE_ON_RESUME);
	#undef SETUSBPIPEPOLICY_BOOL
	#undef SETUSBPIPEPOLICY_DWORD

	#ifdef _DEBUG

	dwRet = sizeof( i );
	WinUsb_GetPipePolicy( ps->pUSB->fd, ps->pUSB->endpoint & 0xFF, MAXIMUM_TRANSFER_SIZE, &dwRet, &i );
	dmsg( "MAX_TRANSFER_SIZE=%u", i );
	#endif

	ps->hThread = ( HANDLE ) _beginthreadex( NULL, 0, tsthread, ps, 0, NULL );
	if ( INVALID_HANDLE_VALUE == ps->hThread ) {
		warn_info( errno, "tsthread_create failed" );
		uHeapFree( ps->buffer );
		return -1;
	} else {
		SetThreadPriority( ps->hThread, THREAD_PRIORITY_HIGHEST );
	}
	*tptr = ps;

	return 0;
}

void tsthread_destroy(const tsthread_ptr ptr)
{
	int i;
	struct tsthread_param* const p = ptr;

	tsthread_stop(ptr);
	p->flags |= 0x02;    //# canceled = T
	SetEvent(p->hTsRead);
	SetEvent(p->hTsAvailable);
	if (WaitForSingleObject(p->hThread, 1000) != WAIT_OBJECT_0) {
		warn_msg(GetLastError(), "tsthread_destroy timeout");
		TerminateThread(p->hThread, 0);
	}
	for (i = 0; i < TS_MaxNumIO; i++)
		CloseHandle(p->hTsEvents[i]);
	CloseHandle(p->hTsAvailable);
	CloseHandle(p->hTsRead);
	CloseHandle(p->hTsRestart);
	CloseHandle(p->hThread);
	CloseHandle(p->hTsStopped);
#ifdef INCLUDE_ISOCH_XFER
	if(p->hIsochBuffer) {
		WinUsb_UnregisterIsochBuffer( p->hIsochBuffer );
		p->hIsochBuffer = NULL;
	}
#endif
	DeleteCriticalSection(&p->csTsExclusive);

	uHeapFree(p->buffer);
}

void tsthread_start(const tsthread_ptr ptr)
{
	struct tsthread_param* const ps = ptr;

	EnterCriticalSection(&ps->csTsExclusive) ;
	lockWinUsb(ps,1);

	WinUsb_FlushPipe(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF);
	ps->flags &= ~0x10 ; //# reset isochronous continuous status

#ifdef INCLUDE_ISOCH_XFER
	if( (ps->pUSB->endpoint & 0x100) && !ps->hIsochBuffer) { //# Isochronous
		if (!WinUsb_RegisterIsochBuffer(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF,
			ps->buffer, ps->buff_size, &(ps->hIsochBuffer))) {
			DWORD dwRet = GetLastError();
			ps->hIsochBuffer = NULL;
			warn_info(dwRet, "WinUsb_RegisterIsochBuffer failed");
			DBGOUT("-*- Isoch Buffer Creation Failed -*-\n") ;
		}else {
			DBGOUT(
				"Isoch buffer size = %d (unit size = %d, unit num = %d)\n",
				ps->buff_size, ps->buff_unitSize, ps->buff_num);
		}
	}
#endif

	ps->flags |= 0x01;    //# continue = T
	if (ps->pUSB->startstopFunc)
		ps->pUSB->startstopFunc(ps->pUSB->dev, 1);

	SetEvent(ps->hTsAvailable);

	lockWinUsb(ps,0);
	LeaveCriticalSection(&ps->csTsExclusive) ;
}

void tsthread_stop(const tsthread_ptr ptr)
{
	struct tsthread_param* const ps = ptr;

	EnterCriticalSection(&ps->csTsExclusive) ;
	lockWinUsb(ps,1);

	ResetEvent(ps->hTsStopped);
	ps->flags &= ~0x01U;    //# continue = F

	if(ps->pUSB->startstopFunc)
		ps->pUSB->startstopFunc(ps->pUSB->dev, 0);
#ifdef INCLUDE_ISOCH_XFER
	if(ps->hIsochBuffer) {
		WinUsb_UnregisterIsochBuffer( ps->hIsochBuffer );
		ps->hIsochBuffer = NULL;
	}
#endif

	if(!(ps->pUSB->endpoint & 0x100) ) { //# Bulk
		WinUsb_AbortPipe(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF);
	}

	SetEvent(ps->hTsRestart) ;

	lockWinUsb(ps,0);
	LeaveCriticalSection(&ps->csTsExclusive) ;

	if(!(ps->pUSB->endpoint & 0x100) ) { //# Bulk
		WaitForSingleObject(ps->hTsStopped,USBPIPEPOLICY_PIPE_TRANSFER_TIMEOUT);
	}
}

int tsthread_read(const tsthread_ptr tptr, void ** const ptr)
{
	struct tsthread_param* const ps = tptr;
	int i, j;

	if(!ptr) {
		//# purge
		SetEvent(ps->hTsRestart);
		return 0 ;
	}

	if(!ps->actual_length)
		return 0 ;

	EnterCriticalSection(&ps->csTsExclusive) ;
	i = tsthread_readable(tptr);
	if(0 < i) {
		j = ps->buff_pop;
		ps->actual_length[ps->buff_pop] = -1;
		*ptr = ps->buffer + (j * ps->buff_unitSize);
		ps->buff_pop = (ps->buff_num - 1 > j) ? j + 1 : 0;
		SetEvent(ps->hTsRead) ;
	}
	LeaveCriticalSection(&ps->csTsExclusive) ;

	return i<0 ? 0:i ;
}

int tsthread_readable(const tsthread_ptr tptr)
{
	struct tsthread_param* const ps = tptr;
	int j ;

	if(!ps->actual_length) return 0 ;
	if(!(ps->flags&0x01U)) return 0;
	if(HasSignal(ps->hTsRestart))
		return 0 ;

	EnterCriticalSection(&ps->csTsExclusive) ;
	j= ps->buff_pop;
	if(0 > j || ps->buff_num <= j) {  //# bug check
		warn_info(j,"ts.buff_pop Out of range");
		j = -1;
	}
	else do {  //# skip empty blocks
		if(!ps->actual_length[j]) {
			ps->actual_length[j]=-1;
		}else break;
		if(ps->buff_num -1 > j) {
			j++;
		}else{
			j = 0;
		}
	} while(j != ps->buff_pop);
	ps->buff_pop = j<0 ? 0 : j ;
	LeaveCriticalSection(&ps->csTsExclusive) ;
	return j<0 ? 0 : ps->actual_length[j];
}

int tsthread_wait(const tsthread_ptr tptr, const int timeout)
{
	struct tsthread_param* const ps = tptr;
	DWORD dRet ;
	if(tsthread_readable(tptr)>0) return 1 ; //# already available
	dRet = WaitForSingleObject( ps->hTsAvailable , timeout );
	if(WAIT_OBJECT_0 == dRet)  return 1;
	else if(WAIT_TIMEOUT == dRet)  return 0;

	warn_info(dRet,"poll failed");
	return -1;
}


/*EOF*/
