#pragma once

#ifdef _DEBUG
#include <stdio.h>
#define DBG_INFO(...) { char d_buff[128]; \
	_snprintf_s(d_buff,128, __VA_ARGS__); \
	::OutputDebugStringA(d_buff);}
#else
#define DBG_INFO(...) void()
#endif
