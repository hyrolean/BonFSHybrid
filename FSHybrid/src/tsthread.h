/* fsusb2i   (c) 2015-2016 trinity19683
  TS USB I/O thread (MS-Windows)
  tsthread.h
  2016-01-04
*/
#pragma once
#include "types_u.h"

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

typedef void *(*write_back_begin_func_t)(int id, size_t max_size, void *arg) ;
typedef void (*write_back_finish_func_t)(int id, size_t wrote_size, void *arg) ;
typedef void (*write_back_purge_func_t)(void *arg) ;

struct write_back_t {
  write_back_begin_func_t begin_func;
  write_back_finish_func_t finish_func;
  write_back_purge_func_t purge_func;
  void *arg;
};

int tsthread_create(tsthread_ptr* const, const struct usb_endpoint_st * const, const struct write_back_t * const);
void tsthread_destroy(const tsthread_ptr);
void tsthread_start(const tsthread_ptr);
void tsthread_stop(const tsthread_ptr);
int tsthread_read(const tsthread_ptr,  void** const ptr);
int tsthread_readable(const tsthread_ptr);
int tsthread_wait(const tsthread_ptr, const int);

/*EOF*/