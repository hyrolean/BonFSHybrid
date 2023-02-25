/* fsusb2i   (c) 2015-2016 trinity19683
  TS buffer, transfer parameters (MS-Windows)
  tsbuff.h
  2016-01-22
*/
#pragma once

#define ROUNDUP(n,w) (((n) + (w)) & ~(unsigned)(w))

#ifdef INCLUDE_ISOCH_XFER // Isochronous & Bulk hybrid

//# Isochronous Frames per a Packet
#define ISOCH_PacketFrames	48

//# Isochronous Frame size (real packet size)
#define ISOCH_FrameSize		(5*188)

//# TS Packet size (faked packet size)
#define TS_PacketSize	(ISOCH_FrameSize*ISOCH_PacketFrames)

#else // Bulk only

//# TS Packet size (real packet size)
#define TS_PacketSize	47752 // org : 46060

#endif

//# TS Buffer size
#define TS_CalcBufSize(PacketSize,Packets)	(ROUNDUP(PacketSize,0x1FF)*Packets)
#define TS_BufSize		TS_CalcBufSize(TS_PacketSize)

//# number of keeping read only bytes before the buffer busy memory area
#define TS_CalcDeadZone(BufSize)	(BufSize/4)
#define TS_DeadZone		TS_CalcDeadZone(TS_BufSize)

//# max number of submitted IO requests
#define TS_MaxNumIO		256

//# 2020-11-22
//#   Removed the definition "TS_BufPackets".
//#     It's auto calculated on tsthread_create() function of "tsthread.c".
//# 2020-11-3
//#   Moved the definition "ROUNDUP" from the source file "tsthread.c".
//#   Removed the definition "TS_PollTimeout"
//#     that is moved to "TSTHREAD_POLL_TIMEOUT" as variable on "tsthread.c".
//#   Removed the definition "TS_SubmitTimeout"
//#     that is moved to "TSTHREAD_SUBMIT_TIMEOUT" as variable on "tsthread.c".
//#   Removed the definition "TS_DeltaSize".
//# 2020-10-31
//#   Added the definition "TS_SubmitTimeout".
//# 2020-10-5
//#   Added the definition "ISOCH_PacketFrames".
//#   Added the definition "ISOCH_FrameSize" for the isochronous transfer.
//# 2018-3-1
//#   Added the definition "TS_DeadZone".
//#   Moved the definition "TS_BulkSize" from the source file "it9175.c".
//#   Rename the definition "TS_BulkSize" to "TS_PacketSize".
//# Fixed by 2018-2020 LVhJPic0JSk5LiQ1ITskKVk9UGBg

/*EOF*/