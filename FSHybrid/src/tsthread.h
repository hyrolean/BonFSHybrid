/* fsusb2i   (c) 2015-2016 trinity19683
  TS USB I/O thread (MS-Windows)
  tsthread.h
  2016-01-04
*/
#pragma once
#include "types_u.h"

//# URB thread priority
extern int TSTHREAD_PRIORITY;

//# pipe policy settings
extern BOOL USBPIPEPOLICY_RAW_IO;
extern BOOL USBPIPEPOLICY_AUTO_CLEAR_STALL;
extern BOOL USBPIPEPOLICY_ALLOW_PARTIAL_READS;
extern BOOL USBPIPEPOLICY_AUTO_FLUSH;
extern BOOL USBPIPEPOLICY_IGNORE_SHORT_PACKETS;
extern BOOL USBPIPEPOLICY_SHORT_PACKET_TERMINATE;
extern DWORD USBPIPEPOLICY_PIPE_TRANSFER_TIMEOUT;
extern BOOL USBPIPEPOLICY_RESET_PIPE_ON_RESUME;

typedef void* tsthread_ptr;

typedef void *(*tsfifo_writeback_begin_t)(int id, size_t max_size, void *arg) ;
typedef void (*tsfifo_writeback_finish_t)(int id, size_t wrote_size, void *arg) ;
typedef void (*tsfifo_writethrough_t)(const void *buffer, size_t size, void *arg) ;
typedef void (*tsfifo_purge_t)(void *arg) ;

struct tsfifo_t {
  //# For the write-back caching  
  tsfifo_writeback_begin_t      writeBackBegin;
  tsfifo_writeback_finish_t     writeBackFinish;
  //# For the write-through caching  
  tsfifo_writethrough_t         writeThrough;
  //# For purging the fifo cache
  tsfifo_purge_t                purge;
  //# App's argument to be called back
  void *                        arg;
};

int tsthread_create(tsthread_ptr* const, const struct usb_endpoint_st * const, const struct tsfifo_t *const);
void tsthread_destroy(const tsthread_ptr);
void tsthread_start(const tsthread_ptr);
void tsthread_stop(const tsthread_ptr);
int tsthread_read(const tsthread_ptr,  void** const ptr);
int tsthread_readable(const tsthread_ptr);
int tsthread_wait(const tsthread_ptr, const int);

/*EOF*/
