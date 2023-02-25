/* fsusb2i   (c) 2015-2016 trinity19683
  TS buffer, transfer parameters (MS-Windows)
  tsbuff.h
  2016-01-22
*/
#pragma once


#ifdef INCLUDE_ISOCH_XFER // Isochronous & Bulk hybrid

//# Isochronous Frames per a Packet
#define ISOCH_PacketFrames 48

//# Isochronous Frame size (real packet size)
#define ISOCH_FrameSize   (5*188)

//# TS Packet size (faked packet size)
#define TS_PacketSize   (ISOCH_FrameSize*ISOCH_PacketFrames)

#else // Bulk only

//# TS Packet size (real packet size)
#define TS_PacketSize   47752 // org : 46060

#endif

//# TS Delta size that is reserved bytes protected from device's messy overlapping
#define TS_DeltaSize  	1024

//# TS Buffer packets
#define TS_BufPackets	64

//# TS Buffer size
#define TS_CalcBufSize(PacketSize)	(((PacketSize+0x1FF)&~0x1FF)*TS_BufPackets)
#define TS_BufSize		TS_CalcBufSize(TS_PacketSize)

//# number of compensated read only bytes before the buffer busy memory area
#define TS_CalcDeadZone(BufSize)	(BufSize/4)
#define TS_DeadZone  	TS_CalcDeadZone(TS_BufSize)

//# max number of submitted IO requests
#define TS_MaxNumIO  24

//# IO polling timeout (msec)
#define TS_PollTimeout  100


//# 2020-10-5
//#   Added the definition "ISOCH_PacketFrames".
//#   Added the definition "ISOCH_FrameSize" for the isochronous transfer.
//# 2018-3-1
//#   Added the definition "TS_DeadZone".
//#   Moved the definition "TS_BulkSize" from the source file "it9175.c".
//#   Rename the definition "TS_BulkSize" to "TS_PacketSize".
//# Fixed by 2018-2020 LVhJPic0JSk5LiQ1ITskKVk9UGBg

/*EOF*/
