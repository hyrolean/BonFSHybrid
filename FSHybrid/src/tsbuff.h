/* fsusb2i   (c) 2015-2016 trinity19683
  TS buffer, transfer parameters (MS-Windows)
  tsbuff.h
  2016-01-22
*/
#pragma once


//# TS Packet size (real packet size)
#define TS_PacketSize   48128 //26624 //122880

//# TS Delta size that is reserved bytes protected from device's messy overlapping
#define TS_DeltaSize  1024

//# TS Buffer size
#define TS_BufSize  (((TS_PacketSize+0x1FF)&~0x1FF)*48)

//# number of compensated read only bytes before the buffer busy memory area
#define TS_DeadZone  (TS_BufSize/4)

//# max number of submitted IO requests
#define TS_MaxNumIO  36

//# IO polling timeout (msec)
#define TS_PollTimeout  200

//# 2018-3-1
//#   Added the definition "TS_DeadZone".
//#   Moved the definition "TS_BulkSize" from the source file "it9175.c".
//#   Rename the definition "TS_BulkSize" to "TS_PacketSize".
//# Fixed by 2018 LVhJPic0JSk5LiQ1ITskKVk9UGBg

/*EOF*/