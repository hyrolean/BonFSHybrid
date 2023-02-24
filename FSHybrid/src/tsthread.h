/* fsusb2i   (c) 2015-2016 trinity19683
  TS USB I/O thread (MS-Windows)
  tsthread.h
  2016-01-04
*/
#pragma once
#include "types_u.h"


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