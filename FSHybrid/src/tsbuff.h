/* SunPTV-USB   (c) 2016 trinity19683
  TS buffer, transfer parameters (MS-Windows)
  tsbuff.h
  2016-01-23
*/
#pragma once

//# TS Buffer size = 3584 KiB
#define TS_BufSize  3670016

//# max number of submitted IO requests
#define TS_MaxNumIO  24
//# IO polling timeout (msec)
#define TS_PollTimeout  100

/*EOF*/