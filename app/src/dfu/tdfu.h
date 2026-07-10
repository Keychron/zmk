#ifndef __MY_DFU_H__
#define __MY_DFU_H__


#define MY_DFU_TRACE_ENABLE 1
#define MY_DFU_DIRECT_TRACE 0
#define MY_DFU_RPT_INPUT_ID 0xb1

#define MY_DFU_WATCH_DOG_TIMEOUT_MS 90000

#ifndef UINT32
#define UINT32 unsigned int 
#endif

#ifndef UINT8
#define UINT8 unsigned char
#endif

#ifndef INT32
#define INT32 int
#endif 

#ifndef NULL
#define NULL 0
#endif
enum
{
	SUPPORT_LE			= 0x01,
	SUPPORT_BR			= 0x01,
	SUPPORT_EDR 		= 0x02,
	SUPPORT_RF			= 0x04,
};

enum
{
	TX__RSP_RX_CMD_SUCCESS 	= 0xA1,	
	TX__OTA_REPORT			= 0xA3, 
};

typedef struct 
{
    unsigned char model_number[10];
    unsigned char BR_LE_mode;
    unsigned char bluetooth_version;
    unsigned char fw_version[10];
    unsigned char hw_version[10];
    unsigned char command_version[2];
} BT_INFORMATION_TYPE;

#define DFU_VERSION_VALUE 0x0001

typedef enum
{
	DFU_ENC_NONE		= 0,
	DFU_ENC_XXTEA,
	DFU_ENC_AES128,
} DFU_ENC_MODE_T;

#define DFU_ENC_SUPPORT DFU_ENC_XXTEA

typedef struct{
	unsigned char dfu_version_l;
	unsigned char dfu_version_h;
	unsigned char encrytion_m;
	//unsigned char bootloader_type;
	//unsigned char bootloader_vid[2];
	//unsigned char bottloader_pid[2];
}tDFU_VERSION;

typedef struct
{
	unsigned char hdr_l;
	unsigned char hdr_h;
	unsigned char len;
	unsigned char len_n;
	unsigned char sn;
	unsigned char rsp_cmd;
	unsigned char ack_sn;
	unsigned char ack_cmd;
	unsigned char ack_status;
}CMD_RSP_HDR;
enum
{
	SC_FWU_HEADER = 0xaa55,
	SC_FWU_HEADER_ACK = 0xaa56,
};

enum
{
	SCFWU_OPCODE_GET_MODEL_INFO = 0x60,
	SCFWU_OPCODE_GET_DFU_VERSION = 0x61,
	SCFWU_OPCODE_SET_SC_METHOED = 0x62,
	SCFWU_OPCODE_START = 0x63,
	SCFWU_OPCODE_SEND_BIN = 0x64,
	SCFWU_OPCODE_VERIFY_CRC32 = 0x65,
	SCFWU_OPCODE_IMAGE_SWITCH = 0x66,
	APP_CMD_DFU_BUILD_INFO = 0x6f,
};

void my_scdfu_init(void);
unsigned char my_scdfu_is_active(void);
void my_scdfu_data_handle(unsigned char *pdata,unsigned char rxlen);

#if 1
#define FW_SC_UPGRADE_HID_REPORT_SEND(id,pdata,len)  usb_send_user_if_data(id,pdata,len)
#else
#define FW_SC_UPGRADE_HID_REPORT_SEND(id,pdata,len) 
#endif

#endif
