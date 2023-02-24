#pragma once

#include <WinUSB.h>

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

#define EM2874_TS

#ifdef EM2874_TS
#define USBBULK_XFERSIZE	(0xBC00)
#define RINGBUFF_SIZE	60
#define NUM_IOHANDLE	16
#endif

typedef struct _TSIO_CONTEXT {

    OVERLAPPED ol;
	unsigned index;

} TSIO_CONTEXT;

class EM2874Device
{
private:
	EM2874Device ();
	bool resetICC_1 ();
	bool resetICC_2 ();
	
	HANDLE dev;
	WINUSB_INTERFACE_HANDLE usbHandle;
	uint8_t cardPCB;

#ifdef EM2874_TS
	int BeginAsyncRead();
	int GetOverlappedResult();

	uint8_t *TsBuff;
	volatile int32_t *TsBuffSize;
	int TsBuffIndex;
	int OverlappedIoIndex;
	HANDLE hTsEvent;
	TSIO_CONTEXT IoContext[NUM_IOHANDLE];
	DWORD dwtLastRead;
#endif

public:
	EM2874Device (HANDLE hDev);
	~EM2874Device ();
	static EM2874Device* AllocDevice(int &idx);
	bool initDevice ();
	bool initDevice2 ();

	uint8_t readReg (const uint8_t idx);
	bool readReg (const uint8_t idx, uint8_t *val);
	bool writeReg (const uint8_t idx, const uint8_t val);
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
	void SetBuffer(void *pBuf);
	bool TransferStart();
	void TransferStop();
	void TransferPause();
	void TransferResume();
	int DispatchTSRead();
	HANDLE GetHandle();
#endif
};
