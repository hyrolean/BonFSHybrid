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

#define STRICTLY_CHECK_EVENT_SIGNALS
//#<OFF>#define STRICTLY_CHECK_EMPTY_FRAMES
//#<OFF>#define STRICTLY_CHECK_EMPTY_FRAMES_ALL

#define HasSignal(e) (WaitForSingleObject(e,0)==WAIT_OBJECT_0)


//# URB thread settings
BOOL TSTHREAD_DUPLEX = FALSE ;
int TSTHREAD_PRIORITY = THREAD_PRIORITY_HIGHEST ;
DWORD TSTHREAD_POLL_TIMEOUT = 25 ;
DWORD TSTHREAD_SUBMIT_TIMEOUT = 50 ;
int TSTHREAD_NUMIO = 24 ; //# number of I/O buffering
#define MIN_IOLIMIT 4
int TSTHREAD_SUBMIT_IOLIMIT = MIN_IOLIMIT ; //# keeping number of I/O buffering at least
BOOL TSTHREAD_LOCK_ON_WINUSB = TRUE ;

//# power policy disable suspending
BOOL USBPOWERPOLICY_AVOID_SUSPEND = FALSE ;

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
	HANDLE hThreads[2];    //# handles to thread data
	unsigned char loop_flags ; //# ( 1: reap, 2: submit, 3: both ) <<2: init
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
	int io_num;
	int io_limit;
	int total_submit ;
	struct TSIO_CONTEXT *ioContext ;
	HANDLE *hTsEvents ;
	HANDLE hTsAvailable,hTsRead,hTsRestart,hTsStopped ;

	HANDLE hTsLoopIn, hTsReap, hTsSubmit ;

	struct tsfifo_t* tsfifo ; //# ts-fifo caching object

	int ri; //# the circular index based ioContext cursor for reaping
	int si; //# the circular index based ioContext cursor for submitting

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
	if(TSTHREAD_LOCK_ON_WINUSB&&ps->pUSB->lockunlockFunc)
		 ps->pUSB->lockunlockFunc(ps->pUSB->dev,lock) ;
  }



static void tsthread_purgeURB(const tsthread_ptr ptr)
{
	struct tsthread_param* const ps = ptr;
	int i;

	EnterCriticalSection(&ps->csTsExclusive);
	lockWinUsb(ps,1);

	if( !(ps->pUSB->endpoint & 0x100) ) { //# Bulk

		WinUsb_AbortPipe(ps->pUSB->fd, ps->pUSB->endpoint & 0xFF);

		if(ps->total_submit>0) {

			for (i = 0;i < ps->io_num;i++) {
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
			ps->ri = ps->si ;

		}
	}


	#if 1
	if(ps->tsfifo) {
		if(ps->tsfifo->purge)
			ps->tsfifo->purge(ps->tsfifo->arg);
	}
	if(ps->actual_length) {
		if(!(ps->pUSB->endpoint & 0x100)) { //# Bulk
			memset(&ps->actual_length[0],0xFF,
				ps->buff_num*sizeof(ps->actual_length[0]));
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

//# 2020-11-21
//#   Supported the multi-tasking feature for the reaping and the submitting.
//# 2020-10-5
//#   Added the URB feature for the isochronous transfer mode.
//# 2018-3-1
//#   Removed the tsthread_submitURB static function.
//#   Removed the tsthread_reapURB static function.
//#   Added the tsthread_bulkURB static function instead of submitURB/reapURB.
//# Fixed by 2018-2020 LVhJPic0JSk5LiQ1ITskKVk9UGBg
static unsigned int tsthread_bulkURB(struct tsthread_param* const ps)
{
	BOOL bRet ;
	DWORD dRet=0 ;

	//# the pointer of a ts-fifo caching object
	struct tsfifo_t *pTSFifo = ps->tsfifo ;

	//# identify the loop model which is reaping, submitting or both.
	const int loop_model = (ps->loop_flags >> 2) & 3 ;

	//# whether the current task mode is duplex or not.
	const BOOL duplex = (loop_model&3)==3 ? FALSE : TRUE ;

	//# whether the write-through ts-fifo caching feature is existed or not
	const BOOL hasWThrough = pTSFifo && pTSFifo->writeThrough ? TRUE : FALSE ;

	//# whether the write-back ts-fifo caching feature is enabled or not
	const BOOL isWBack =
#ifdef INCLUDE_ISOCH_XFER
			(ps->pUSB->endpoint & 0x100) ? FALSE:
#endif
			(pTSFifo && pTSFifo->writeBackBegin && pTSFifo->writeBackFinish ? TRUE: FALSE);

	//# number of the prefetching buffer busy area that shouldn't be submitted
	const int POPDELTA = hasWThrough ? 0 :
		(TS_CalcDeadZone(ps->buff_size)+ps->buff_unitSize-1)/ps->buff_unitSize;

	//# prefetching frames per a packet
	const int frames =
#ifdef INCLUDE_ISOCH_XFER
		(ps->pUSB->endpoint & 0x100) ? ISOCH_PacketFrames :
#endif
		1 ;

	EnterCriticalSection(&ps->csTsExclusive);
	ps->loop_flags &= ~(loop_model<<2) ;
	LeaveCriticalSection(&ps->csTsExclusive);

	//# tell the loop is started
	SetEvent(ps->hTsLoopIn);

	//# check the contradiction of caching methods
	assert( !isWBack != (ps->actual_length==NULL) ) ;
	assert( !isWBack || (isWBack && !hasWThrough) ) ;

	//# bulk loop
	while(!(ps->flags&0x02)) {

#ifndef INCLUDE_ISOCH_XFER
		//# shutdown ( isoch is not supported )
		if (ps->pUSB->endpoint & 0x100) { //# Isochronous
			if(ps->flags & 0x01) {
				tsthread_stop(ps);
				continue ;
			}
		}
#endif
		//# stop
		if (!(ps->flags & 0x01)) {
			EnterCriticalSection(&ps->csTsExclusive);
			ps->loop_flags |= loop_model ;  //# loop deactivate 0 -> 1
			if((ps->loop_flags&3)==3) {
				SetEvent(ps->hTsStopped);
				SetEvent(ps->hTsRestart); //# restart to reactivate loops
			}
			LeaveCriticalSection(&ps->csTsExclusive);
			WaitForSingleObject(ps->hTsAvailable, TSTHREAD_POLL_TIMEOUT) ;
			continue ;
		}

		//# restart
		if(HasSignal(ps->hTsRestart)) {
			EnterCriticalSection(&ps->csTsExclusive);
			ps->loop_flags |= loop_model ;  //# loop deactivate 0 -> 1
			if((loop_model&2)&&(ps->loop_flags&3)==3) {
				tsthread_purgeURB(ps) ;
				ResetEvent(ps->hTsRestart) ;
				SetEvent(ps->hTsAvailable) ;
				LeaveCriticalSection(&ps->csTsExclusive);
			}else {
				LeaveCriticalSection(&ps->csTsExclusive);
				WaitForSingleObject(ps->hTsAvailable, TSTHREAD_POLL_TIMEOUT) ;
			}
			continue;
		}else if(ps->loop_flags&loop_model) {
			EnterCriticalSection(&ps->csTsExclusive);
			ps->loop_flags &= ~loop_model ; //# loop activate 1 -> 0
			LeaveCriticalSection(&ps->csTsExclusive);
		}

		//# reaping loop model
		if( loop_model&1 ) {

			if(duplex && ps->total_submit<(ps->flags&0x10?ps->io_limit:MIN_IOLIMIT)) {
				HANDLE events[2];
				events[0]=ps->hTsSubmit;
				events[1]=ps->hTsRestart ;
				//# wait for submitting buffer...
				WaitForMultipleObjects(2, events , FALSE, TSTHREAD_SUBMIT_TIMEOUT );
				if(ps->total_submit<ps->io_limit)
					continue ; //# less than io_limit, redo...
			}

			if(HasSignal(ps->hTsRestart)) continue ;

			if(ps->total_submit>0) {

				//# poll
				int next_wait_index=-1 ;
				{
					int total_submit = ps->total_submit ;
					int max_wait_count = total_submit<MAXIMUM_WAIT_OBJECTS ? total_submit : MAXIMUM_WAIT_OBJECTS ;
					dRet = WaitForMultipleObjects(max_wait_count, &ps->hTsEvents[ps->ri] , FALSE, TSTHREAD_POLL_TIMEOUT );
					if (isCritical(ps)) continue;
					if(WAIT_OBJECT_0 <= dRet&&dRet < WAIT_OBJECT_0+max_wait_count) {
						next_wait_index = ((dRet - WAIT_OBJECT_0)+1 + ps->ri)%ps->io_num ;
#ifdef STRICTLY_CHECK_EVENT_SIGNALS
						if(!duplex) {
							int end_index=(ps->ri+ps->total_submit)%ps->io_num ;
							while( next_wait_index != end_index ) {
								if(!HasSignal(ps->hTsEvents[next_wait_index]))
									break ;
								if (++next_wait_index >= ps->io_num)
									next_wait_index ^= next_wait_index ;
							}
						}
#endif
					}else if(WAIT_TIMEOUT!=dRet) {
						dRet = GetLastError();
						warn_info(dRet,"poll failed");
						break;
					}
				}

				//# reap
				if(next_wait_index>=0) do {

					struct TSIO_CONTEXT* pContext = &ps->ioContext[ps->ri];
					if(pContext->index>=0) {

						DWORD bytesRead=pContext->bytesRead ;

						if (duplex && !HasSignal(ps->hTsEvents[ps->ri])) break ;

						if (isCritical(ps)) break;
						//if(!HasSignal(ps->hTsEvents[ps->ri])) break ;
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
							warn_msg(dRet, "reapURB%u failed", ps->ri);
						}
						if(pContext->index>=0) {
							if (ps->pUSB->endpoint & 0x100) {
#ifdef INCLUDE_ISOCH_XFER
								EnterCriticalSection(&ps->csTsExclusive) ;
								if(hasWThrough && pContext->index==ps->buff_pop) {

									//# write-through caching ( context & buff_pop indices sync )
									char *p = &ps->buffer[pContext->index*ps->buff_unitSize] ;
									const USBD_ISO_PACKET_DESCRIPTOR *pDesc=&pContext->isochFrameDesc[0];
									int n, sz, amount = 0 ;
									if(!bRet)
										warn_msg(dRet, "reapURB%u(%u)",ps->ri, frames);
									else for(n=frames ; n-- ; pDesc++) {
										if(pDesc->Status) {
											sz = 0;
											warn_msg(dRet, "reapURB%u.%u", ps->ri, frames-1-n);
										}else
											sz = pDesc->Length ;
										if(n&&sz==ps->buff_unitSize)
											amount+=sz ;
										else {
											if(amount+sz>0)
												pTSFifo->writeThrough(p, amount+sz, pTSFifo->arg) ;
											p+=amount+ps->buff_unitSize, amount=0 ;
										}
									}
									if(ps->buff_pop + frames >= ps->buff_num)
										ps->buff_pop = 0;
									else
										ps->buff_pop += frames ;
									ps->actual_length[pContext->index]=-1 ;

#ifdef STRICTLY_CHECK_EMPTY_FRAMES
									if(frames>1) {
#ifdef STRICTLY_CHECK_EMPTY_FRAMES_ALL
										//# reset all frames
										__stosd(&ps->actual_length[pContext->index+1],-1,frames-1);
#else
										//# erase a gatekeeper frame
										ps->actual_length[pContext->index+frames-1]=-1 ;
#endif
									}
#endif
									LeaveCriticalSection(&ps->csTsExclusive) ;
								}else {
									LeaveCriticalSection(&ps->csTsExclusive) ;
#if defined(_WIN32) && !defined(_WIN64)

									int stride = (char*) &pContext->isochFrameDesc[1]
										- (char*) &pContext->isochFrameDesc[0] ;
									void* sp_ = &pContext->isochFrameDesc[0].Length ;
									void* dp_ = &ps->actual_length[pContext->index] ;
									int dx_ = (char*) &pContext->isochFrameDesc[0].Status
										- (char*) &pContext->isochFrameDesc[0].Length ;
									int errors = 0 ;
									_asm {
										mov ecx, frames
										mov edi, dp_
										cld
										cmp bRet, 0
										je lb3
										xor ebx, ebx
										mov esi, sp_
										mov eax, stride
										mov edx, dx_
									lb1:
										cmp dword ptr [esi+edx], 0
										jne lb2
										movsd
										lea esi, [esi+eax-4]
										dec ecx
										jnz lb1
										jmp lb4
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
										mov errors, ebx
									}
									if(errors>0)
										warn_msg(dRet, "reapURB%u(%u)",ps->ri, errors);
#else
									register int n,*pLen = &ps->actual_length[pContext->index];
									register const USBD_ISO_PACKET_DESCRIPTOR *pDesc=&pContext->isochFrameDesc[frames-1];
									if(!bRet) {
										__stosd(pLen,0,frames);
										warn_msg(dRet, "reapURB%u(%u)",ps->ri, frames);
									}
									else for (n = frames ; n ; n--, pDesc--) {
										if (pDesc->Status) {
											pLen[n-1] =  0;
											warn_msg(dRet, "reapURB%u.%u", ps->ri, n-1);
											continue;
										}
										pLen[n-1] = pDesc->Length ;
									}
#endif
								}
								if(bRet) SetEvent(ps->hTsAvailable) ;
#endif
							}else {
								if (isWBack)
									pTSFifo->writeBackFinish(ps->ri, bytesRead, pTSFifo->arg);
								else
									ps->actual_length[pContext->index] = bytesRead;
								if(bytesRead) SetEvent(ps->hTsAvailable) ;
							}
							ResetEvent(ps->hTsEvents[ps->ri]);
							pContext->index=-1 ;
							if(duplex) EnterCriticalSection(&ps->csTsExclusive) ;
							ps->total_submit-- ;
							if(duplex) LeaveCriticalSection(&ps->csTsExclusive) ;
							SetEvent(ps->hTsReap);
						}
					}
					if(++ps->ri>=ps->io_num) ps->ri^=ps->ri ;
					if(ps->total_submit<=0) break;

				}while(duplex || ps->ri!=next_wait_index);

			}
		}

		//# write-through caching ( async )
		if( (loop_model&1) && hasWThrough ) {

			char *p = NULL, *r ;
			int sz, amount = 0 ;
			do {
				r = NULL ;
				sz  = tsthread_read( ps, (void**) &r ) ;
				if(r) {
					if(!p)
						p=r, amount=sz, sz=0 ;
					else if(&p[amount]==r)
						amount+=sz, sz=0 ;
				}
				if(!r||sz>0) {
					if(p) pTSFifo->writeThrough(p, amount, pTSFifo->arg) ;
					p=r, amount=sz ;
				}
			}while(p) ;

		}

		//# submitting loop model
		if( loop_model&2 ) {

			if( duplex && ps->total_submit>=(ps->flags&0x10?ps->io_num:MIN_IOLIMIT) ) {
				HANDLE events[2];
				events[0]=ps->hTsReap;
				events[1]=ps->hTsRestart ;
				//# wait for reaping buffer...
				WaitForMultipleObjects(2, events , FALSE, TSTHREAD_POLL_TIMEOUT );
				if(ps->total_submit>=ps->io_num)
					continue ; //# still full, redo...
			}

			if (HasSignal(ps->hTsRestart)) continue;

			//# submit
			if( ps->total_submit<ps->io_num && (ps->flags & 0x01) ) {
				DWORD tick = GetTickCount() ;
				int fulfill = duplex || !(ps->flags&0x10) ;
				void *buffer;
				DWORD lnTransfered;
				int num_empties=0,max_empties=ps->io_num;
				int last_state=0;
				dRet = 0;bRet = FALSE;
				//# calculate the real maximum number of empties on the buffer
				if(!isWBack) {
					EnterCriticalSection(&ps->csTsExclusive) ;
					tsthread_readable(ps);
					last_state = ps->actual_length[ps->buff_pop] ;
					#if 1
					if(ps->buff_push==ps->buff_pop)
						max_empties =
							last_state==-1 ? ps->buff_num : 0 ;
					else
					#endif
						max_empties = ps->buff_push<ps->buff_pop ?
							ps->buff_pop-ps->buff_push :
							ps->buff_num-ps->buff_push + ps->buff_pop ;
					ResetEvent(ps->hTsRead);
					LeaveCriticalSection(&ps->csTsExclusive) ;
					max_empties -= POPDELTA; //# subtract deadzone
				}
				//# total I/O submittable empties
				num_empties=ps->io_num-ps->total_submit;
				num_empties*=frames;
				//start=ps->si ;
				if(num_empties>max_empties) {
					num_empties = max_empties ;
					#if 1
					if(num_empties<=0&&ps->total_submit<=0) {
						//# in the dead zone
						if(!isWBack&&!hasWThrough&&last_state>=0) {
							HANDLE events[2];
							events[0]=ps->hTsRead;
							events[1]=ps->hTsRestart ;
							//# wait for reading buffer...
							dRet=WaitForMultipleObjects(2, events , FALSE, TSTHREAD_POLL_TIMEOUT );
							if(dRet==WAIT_TIMEOUT)
							  DBGOUT("read: wait timeout\n") ;
							else if(dRet==WAIT_OBJECT_0)
							  DBGOUT("read: wait success\n") ;
						}
					}
					#endif
				}
				//# submit to empties
				while (num_empties>=frames) {
					BOOL submitted = FALSE ;
					struct TSIO_CONTEXT* pContext = &ps->ioContext[ps->si];
					if (isCritical(ps)) break;
					if(ps->total_submit>=ps->io_num) break;
					if (pContext->index>=0) break ;
					//if (HasSignal(ps->hTsEvents[ps->ri])) break;
#ifndef INCLUDE_ISOCH_XFER
					if (ps->pUSB->endpoint & 0x100) { //# Isochronous
						//tsthread_stop(ps);
						break;
					}
#endif
					if(isWBack) {
						buffer = pTSFifo->writeBackBegin(ps->si, ps->buff_unitSize, pTSFifo->arg) ;
						if(!buffer) break ; //# buffer overflow
					}
#ifdef STRICTLY_CHECK_EMPTY_FRAMES
					else {
						register int n,*p=&ps->actual_length[ps->buff_push] ;
						if(frames==1)
							n = *p==-1 ? 0: 1;
						else {
#if defined(_WIN32) && !defined(_WIN64)
							_asm {
								mov eax, -1
								mov edi, p
								mov ecx, frames
								cld
								repe scasd //# dw string scan
								mov n, ecx
							}
#else
#if defined(_WIN32) && defined(_WIN64)
							if(!(frames&1)) for(n=frames*sizeof(*p)/sizeof(__int64);n;n--) {
								if(((__int64*)p)[n-1]!=-1) break ; //# qw scan
							}else
#endif
							for(n=frames;n;n--) { if(p[n-1]!=-1) break; }
#endif
						}
						if(n) break ; //# buffer busy
					}
#endif
					if(!isWBack) {
						buffer = ps->buffer + ps->buff_push * ps->buff_unitSize ;
						last_state = ps->actual_length[ps->buff_push] ;
						ps->actual_length[ps->buff_push] = -2;
					}
					pContext->index = isWBack ? ps->si : ps->buff_push;
					pContext->bytesRead = 0 ;
					ZeroMemory(&pContext->ol,sizeof(OVERLAPPED));
					pContext->ol.hEvent = ps->hTsEvents[ps->si];
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
								ps->buff_push * ps->buff_unitSize, /*offset*/
								frames * ps->buff_unitSize,  /*length*/
								ps->flags & 0x10 ? TRUE: FALSE, /*continuous*/
								frames, pContext->isochFrameDesc,
								&(pContext->ol));
							dRet = GetLastError();
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
							pTSFifo->writeBackFinish(ps->si,0,pTSFifo->arg);
						else
							ps->actual_length[ps->buff_push] = last_state;
						ResetEvent(ps->hTsEvents[ps->si]);
						pContext->index = -1;
						if(ps->flags&0x10) ps->flags &= ~0x10; //# not cont
					}else {
						//# submitting succeeded
						if(!isWBack) {
#ifdef STRICTLY_CHECK_EMPTY_FRAMES
							if(frames>1) {
#ifdef STRICTLY_CHECK_EMPTY_FRAMES_ALL
								//# fill all frames
								__stosd(&ps->actual_length[ps->buff_push+1],-2,frames-1);
#else
								//# make a gatekeeper frame
								ps->actual_length[ps->buff_push+frames-1]=-2;
#endif
							}
#endif
							if(ps->buff_push+frames>=ps->buff_num)
								ps->buff_push=0;
							else
								ps->buff_push+=frames;
						}
						if(bRet) {
							DBGOUT("submit: success (size=%d)\n",lnTransfered) ;
							pContext->bytesRead = lnTransfered ;
							SetEvent(ps->hTsEvents[ps->si]) ;
						}else {
							if(lnTransfered>0)
								DBGOUT("submit: pending (size=%d)\n",lnTransfered) ;
						}
						dRet = 0;
						if(++ps->si >= ps->io_num) ps->si^=ps->si;
						if(duplex) EnterCriticalSection(&ps->csTsExclusive) ;
						++ps->total_submit;
						if(duplex) LeaveCriticalSection(&ps->csTsExclusive) ;
						SetEvent(ps->hTsSubmit) ;
						submitted=TRUE;
						if(!(ps->flags&0x10)) ps->flags |= 0x10; //# cont
					}
					//# check submitting failed or not
					if(!submitted) break ;
					num_empties-=frames;
					if(fulfill) continue;
					if (GetTickCount()-tick>=TSTHREAD_SUBMIT_TIMEOUT
						&& ps->total_submit>=ps->io_limit )
						break; //# submitting timeout
				}
				//if(dRet) break ;
			}

		}

	}

	EnterCriticalSection(&ps->csTsExclusive);
	ps->loop_flags |= (loop_model<<4) ; //# end of loop
	if((ps->loop_flags&(3<<4))==(3<<4)) tsthread_purgeURB(ps); //# dispose
	LeaveCriticalSection(&ps->csTsExclusive);

	return dRet ;
}

/* TS thread function issues URB requests. */
static unsigned int __stdcall tsthread(void* const param)
{
	struct tsthread_param* const ps = param;
	unsigned int result = 0;

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

	int result = 0 ;
	struct tsthread_param* ps;
	DWORD dwRet; int i;

	unsigned io_num = TSTHREAD_NUMIO ;
	unsigned io_limit = TSTHREAD_SUBMIT_IOLIMIT ;
	unsigned TS_BufPackets = 1 ;
	if(io_limit<MIN_IOLIMIT) io_limit=MIN_IOLIMIT ;
	if (io_num < io_limit) io_num = io_limit;
	else if (io_num > TS_MaxNumIO) io_num = TS_MaxNumIO;
	if(io_limit > io_num) io_limit = io_num ;
	while(TS_BufPackets<io_num+TS_CalcDeadZone(TS_BufPackets))
		TS_BufPackets<<=1 ;

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
			TS_CalcBufSize(xferSize,TS_BufPackets)/unitSize ;
		const unsigned buffSize = wback_exists ? 0 : unitSize*unitNum ;
		const unsigned buffer_size = wback_exists ? 0 : ROUNDUP( buffSize, 0xF );
		const unsigned actlen_size = wback_exists ? 0 : sizeof( int ) * unitNum;
		const unsigned tsfifo_size = tsfifo_exists ? ROUNDUP( sizeof( struct tsfifo_t ), 0xF) : 0;
		const unsigned iocontext_size = ROUNDUP( io_num * sizeof(struct TSIO_CONTEXT), 0xF);
		const unsigned htsevents_size = ROUNDUP(  (io_num * 2 - 1) * sizeof(HANDLE), 0xF);
		char *ptr, *buffer_ptr;
		struct tsfifo_t *tsfifo_ptr;
		unsigned totalSize =  param_size + actlen_size + buffer_size + tsfifo_size + iocontext_size + htsevents_size ;
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
		ps->ioContext = ( struct TSIO_CONTEXT * ) ptr;
		ptr += iocontext_size;
		ps->hTsEvents = (HANDLE*) ptr ;
		ptr += htsevents_size;
		ps->xfer_size= xferSize;
		ps->buff_unitSize = unitSize;
		ps->buff_num = unitNum;
		ps->buff_size = buffSize;
		ps->io_num = io_num ;
		ps->io_limit = io_limit ;
		assert(ptr-buffer_ptr==totalSize) ;
		if ( actlen_size ) {
			//# reset all values to empty
			memset(&ps->actual_length[0],0xFF,actlen_size);
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
	ps->buff_push = 0;
	ps->buff_pop = 0;
	ps->total_submit = 0;
	ps->ri = 0;
	ps->si = 0;
#ifdef INCLUDE_ISOCH_XFER
	ps->hIsochBuffer = NULL;
	if (pusbep->endpoint & 0x100)
		DBGOUT("-*- ISOCHRONOUS TRANSFER MODE -*-\n") ;
#else
	if (pusbep->endpoint & 0x100) { //# Isochronous
		warn_msg(0, "Please change to BULK transfer mode :-P");
	}
#endif

	for ( i = 0; i < ps->io_num; i++ ) {
		ps->ioContext[ i ].index = -1;    //# mark it unused
		ps->hTsEvents[ i ] = CreateEvent( NULL, TRUE, FALSE, NULL );
		ZeroMemory( &ps->ioContext[ i ].ol, sizeof( OVERLAPPED ) );
		ps->ioContext[ i ].ol.hEvent = ps->hTsEvents[ i ];
	}

	//# it arranges for event handles to look like circular buffer
	for ( i = 0; i < ps->io_num - 1; i++ ) {
		ps->hTsEvents[ i + ps->io_num ] = ps->hTsEvents[ i ];
	}
	ps->hTsAvailable = CreateEvent( NULL, FALSE, FALSE, NULL );
	ps->hTsRead = CreateEvent( NULL, FALSE, FALSE, NULL );
	ps->hTsRestart = CreateEvent( NULL, TRUE, FALSE, NULL );
	ps->hTsStopped = CreateEvent( NULL, FALSE, FALSE, NULL );
	ps->hTsLoopIn = CreateEvent( NULL, TRUE, FALSE, NULL ) ;
	ps->hTsReap = CreateEvent( NULL, FALSE, FALSE, NULL );
	ps->hTsSubmit = CreateEvent( NULL, FALSE, FALSE, NULL );
	InitializeCriticalSection(&ps->csTsExclusive) ;

	//# set USB pipe policy settings
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

	//# set USB power policy settings
	if(USBPOWERPOLICY_AVOID_SUSPEND) {
		ULONG delay = INFINITE ;
		UCHAR suspend = 0 ;
		WinUsb_SetPowerPolicy(pusbep->fd,SUSPEND_DELAY,sizeof(delay),&delay);
		WinUsb_SetPowerPolicy(pusbep->fd,AUTO_SUSPEND,sizeof(suspend),&suspend);
	}

	#ifdef _DEBUG

	dwRet = sizeof( i );
	WinUsb_GetPipePolicy( ps->pUSB->fd, ps->pUSB->endpoint & 0xFF, MAXIMUM_TRANSFER_SIZE, &dwRet, &i );
	dmsg( "MAX_TRANSFER_SIZE=%u", i );
	#endif

	SetEvent(ps->hTsRestart);

	ps->loop_flags = 0 ;
	ps->hThreads[0]=ps->hThreads[1]=INVALID_HANDLE_VALUE;
	for(i=0;i<(TSTHREAD_DUPLEX?2:1);i++) {
		EnterCriticalSection(&ps->csTsExclusive);
		ps->loop_flags |= TSTHREAD_DUPLEX ? 1<<(2+i)/*duplex*/: 3<<2/*simplex*/;
		LeaveCriticalSection(&ps->csTsExclusive);
		ResetEvent(ps->hTsLoopIn);
		ps->hThreads[i] = ( HANDLE ) _beginthreadex( NULL, 0, tsthread, ps, 0, NULL );
		if ( INVALID_HANDLE_VALUE == ps->hThreads[i] ) {
			warn_info( errno, "tsthread_create(%d) failed",i );
			result = -1 ;
		}else {
			SetThreadPriority( ps->hThreads[i], TSTHREAD_PRIORITY );
			if(TSTHREAD_DUPLEX) WaitForSingleObject(ps->hTsLoopIn,INFINITE);
		}
	}

	*tptr = ps;

	return result ;
}

void tsthread_destroy(const tsthread_ptr ptr)
{
	int i;
	struct tsthread_param* const p = ptr;

	tsthread_stop(ptr);
	p->flags |= 0x02;    //# canceled = T
	SetEvent(p->hTsRead);
	SetEvent(p->hTsAvailable);
	SetEvent(p->hTsReap);
	SetEvent(p->hTsSubmit);
	for(i=0;i<2;i++) {
		if(p->hThreads[i]!=INVALID_HANDLE_VALUE) {
			if (WaitForSingleObject(p->hThreads[i],
					USBPIPEPOLICY_PIPE_TRANSFER_TIMEOUT) != WAIT_OBJECT_0) {
				warn_msg(GetLastError(), "tsthread_destroy timeout(%d)",i);
				TerminateThread(p->hThreads[i], 0);
			}
			CloseHandle(p->hThreads[i]);
		}
	}
	for (i = 0; i < p->io_num; i++)
		CloseHandle(p->hTsEvents[i]);
	CloseHandle(p->hTsAvailable);
	CloseHandle(p->hTsRead);
	CloseHandle(p->hTsRestart);
	CloseHandle(p->hTsStopped);
	CloseHandle(p->hTsLoopIn);
	CloseHandle(p->hTsReap);
	CloseHandle(p->hTsSubmit);
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

	SetEvent(ps->hTsRestart);

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
	char *p ;
	int i, j ;

	if(!ptr) {
		//# purge
		SetEvent(ps->hTsRestart);
		return 0 ;
	}

	if(!ps->actual_length)
		return 0 ;

	p = NULL;

	EnterCriticalSection(&ps->csTsExclusive) ;

	i = tsthread_readable(tptr);
	if(i>0) {
		j = ps->buff_pop;
		p = ps->buffer + (j * ps->buff_unitSize);
		ps->actual_length[ps->buff_pop] = -1;
		ps->buff_pop = (ps->buff_num > j+1) ? j+1 : 0;
	}

	LeaveCriticalSection(&ps->csTsExclusive) ;

	*ptr=p ;
	if(p) SetEvent(ps->hTsRead) ;
	return i<0?0:i ;
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
	}else do {  //# skip empty blocks
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
	{
		HANDLE events[2];
		events[0]=ps->hTsRestart ;
		events[1]=ps->hTsAvailable;
		//# wait for buffer available...
		dRet = WaitForMultipleObjects(2, events , FALSE, timeout);
	}
	if(WAIT_OBJECT_0+1 == dRet)  return 1;
	else if(WAIT_OBJECT_0 == dRet || WAIT_TIMEOUT == dRet)  return 0;

	warn_info(dRet,"poll failed");
	return -1;
}


/*EOF*/