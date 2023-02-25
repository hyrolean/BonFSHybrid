#pragma once

#include <WinUSB.h>
extern "C" {
#include "osdepend.h"
}

typedef unsigned char	uint8_t;
typedef unsigned short	uint16_t;
typedef unsigned int	uint32_t;
typedef signed int		int32_t;

#define EM28XX_REG_I2C_CLK		0x06
#define EM2874_REG_TS_ENABLE	0x5f
#define EM28XX_REG_GPIO			0x80

#define EM2874_REG_CAS_STATUS	0x70
#define EM2874_REG_CAS_DATALEN	0x71
#define EM2874_REG_CAS_MODE1	0x72
#define EM2874_REG_CAS_RESET	0x73
#define EM2874_REG_CAS_MODE2	0x75

#define EM2874_EP_TS1		0x84

#define DEMOD_ADDR	0x20
#define EEPROM_ADDR	0xa0
#define TUNER_ADDR	0xc0

/* EM2874 TS Enable Register (0x5f) */
#define EM2874_TS1_CAPTURE_ENABLE 0x01
#define EM2874_TS1_FILTER_ENABLE  0x02
#define EM2874_TS1_NULL_DISCARD   0x04

//#define EM2874_TS

#ifdef EM2874_TS
	#define USBBULK_XFERSIZE	(0xBC00)
	#define RINGBUFF_SIZE	48
	#define NUM_IOHANDLE	36
#endif

#define EM2874_USBEP

#ifdef EM2874_USBEP
extern "C" {
#include "types_u.h"
#include "tsbuff.h"
}
#endif


typedef struct _TSIO_CONTEXT {

	OVERLAPPED ol;
	unsigned index;

} TSIO_CONTEXT;

class EM2874Device
{
#ifdef EM2874_TS
public:
	typedef void *(*write_back_begin_func_t)(int id, size_t max_size, void *arg) ;
	typedef void (*write_back_finish_func_t)(int id, size_t wrote_size, void *arg) ;
	struct tsfifo_t {
		write_back_begin_func_t begin_func;
		write_back_finish_func_t finish_func;
		void *arg;
	};
#endif
private:
	EM2874Device ();
	bool resetICC_1 ();
	bool resetICC_2 ();

	HANDLE dev;
	WINUSB_INTERFACE_HANDLE usbHandle;
	uint8_t cardPCB;

	PMUTEX pmutex;

	BOOL DoUSBCtrlTransfer(
	  WINUSB_SETUP_PACKET     SetupPacket,
	  PUCHAR                  Buffer,
	  ULONG                   BufferLength,
	  PULONG                  LengthTransferred,
	  LPOVERLAPPED            Overlapped,
	  DWORD                   &LastError
	);

#ifdef EM2874_TS
	BOOL DoUSBReadPipe(
	  PUCHAR                  Buffer,
	  ULONG                   BufferLength,
	  PULONG                  LengthTransferred,
	  LPOVERLAPPED            Overlapped,
	  DWORD                   &LastError
	);

	BOOL DoUSBGetOverlappedResult(
	  LPOVERLAPPED            Overlapped,
	  LPDWORD                 NumberOfBytesTransferred,
	  BOOL                    Wait,
	  DWORD                   &LastError
	);

	bool BeginAsyncRead();
	int GetOverlappedResult();

	uint8_t *TsBuff;
	volatile int32_t *TsBuffSize;
	int TsBuffIndex;
	int OverlappedIoIndex;
	HANDLE hTsEvent;
	TSIO_CONTEXT IoContext[NUM_IOHANDLE];
	tsfifo_t TSFifo ;
	DWORD dwtLastRead;
#endif

public:
	EM2874Device (HANDLE hDev);
	~EM2874Device ();
	static EM2874Device* AllocDevice(HANDLE hDev,HANDLE hUsbDev);
	bool initDevice (HANDLE hUsbDev);
	bool initDevice2 ();

	uint8_t readReg (const uint8_t idx);
	bool readReg (const uint8_t idx, uint8_t *val);
	bool writeReg (const uint8_t idx, const uint8_t val);
	int ctrlReg(const uint16_t reg, const uint16_t size, uint8_t* const data, const unsigned mode);
	bool readI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop);
	bool writeI2C (const uint8_t addr, const uint16_t size, uint8_t *data, const bool isStop);

#ifdef EM2874_ICC
	bool resetICC ();
	bool writeICC (const size_t size, const void *data);
	bool readICC (size_t *size, void *data);
	int  waitICC();
	int  getCardStatus();
#endif
	int  getDeviceID();

	bool isCardReady;
	static unsigned UserSettings;

#ifdef EM2874_TS
	bool WriteBackEnabled() ;
	void SetBuffer(void *pBuf, const struct tsfifo_t * const pTSFifo=NULL);
	bool TransferStart();
	void TransferStop();
	void TransferPause();
	void TransferResume();
	int DispatchTSRead();
	HANDLE GetHandle();
	int GetTsBuffIndex();
#endif

#ifdef EM2874_USBEP
	static int USBEndPointStartStopFunc(void * const  dev, const int start);
	static int USBEndPointLockUnlockFunc(void * const  dev, const int lock);
	void SetupUSBEndPoint(usb_endpoint_st *usb_ep) ;
	void CleanupUSBEndPoint(usb_endpoint_st *usb_ep) ;
#endif
};
