
#include "tdfu.h"
#include "tdfu_prv.h"
#include <zephyr/kernel.h>
#if EN_ZEPHYR_DFU
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/dfu/flash_img.h>
#include <usb_work_q.h>
#endif 
#include <zephyr/sys/reboot.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb_dfu, 4);

unsigned char my_scdfu_is_active(void) ;



#define USE_VIA_USAGE_PAGE

static unsigned char sc_dfu_rsp_buf[128];
static unsigned char sc_dfu_sn =1;
static unsigned char sc_dfu_security_mode = DFU_ENC_NONE;
static unsigned char sc_dfu_enable = 0;
static unsigned short sc_dfu_sq=0xffff;
static unsigned char sc_dfu_wait_reboot=0;


UINT32 Dfu_CRC_Val = 0xFFFFFFFF;
UINT32 Dfu_CRC_Val_for_XXTEA = 0xFFFFFFFF;
static unsigned int dfu_location=0;

static unsigned int last_sn = 0;

// #define FLASH_WR_TEMP_BUFF_MAX_IDX 512
static unsigned short flash_wr_temp_idx=0;
// static unsigned char flash_wr_temp_buff[FLASH_WR_TEMP_BUFF_MAX_IDX];

int zmk_usb_hid_send_report(const uint8_t *report, size_t len);
int zmk_usb_hid_via_send(const uint8_t *report, size_t len);
#if EN_ZEPHYR_DFU
#define SLOT0_PARTITION			slot0_partition
#define SLOT1_PARTITION			slot1_partition

#define FIRMWARE_IMAGE_0_LABEL "image_0"
#define FIRMWARE_IMAGE_1_LABEL "image_1"

#define USB_DFU_MAX_XFER_SIZE		32//CONFIG_USB_REQUEST_BUFFER_SIZE
enum dfu_state {
	appIDLE,
	appDETACH,
	dfuIDLE,
	dfuDNLOAD_SYNC,
	dfuDNBUSY,
	dfuDNLOAD_IDLE,
	dfuMANIFEST_SYNC,
	dfuMANIFEST,
	dfuMANIFEST_WAIT_RST,
	dfuUPLOAD_IDLE,
	dfuERROR,
};
enum dfu_status {
	statusOK,
	errTARGET,
	errFILE,
	errWRITE,
	errERASE,
	errCHECK_ERASED,
	errPROG,
	errVERIFY,
	errADDRESS,
	errNOTDONE,
	errFIRMWARE,
	errVENDOR,
	errUSB,
	errPOR,
	errUNKNOWN,
	errSTALLEDPKT
};
#if FIXED_PARTITION_EXISTS(SLOT1_PARTITION)
	#define DOWNLOAD_FLASH_AREA_ID FIXED_PARTITION_ID(SLOT1_PARTITION)
#else
	#define DOWNLOAD_FLASH_AREA_ID FIXED_PARTITION_ID(SLOT0_PARTITION)
#endif
struct dfu_worker_data_t {
	uint8_t buf[USB_DFU_MAX_XFER_SIZE];
	enum dfu_state worker_state;
	uint16_t worker_len;
};
struct dfu_data_t {
	uint8_t flash_area_id;
	uint32_t flash_upload_size;
	/* Number of bytes sent during upload */
	uint32_t bytes_sent;
	uint32_t alt_setting;              /* DFU alternate setting */
	struct flash_img_context ctx;
	enum dfu_state state;              /* State of the DFU device */
	enum dfu_status status;            /* Status of the DFU device */
	uint16_t block_nr;                 /* DFU block number */
	uint16_t bwPollTimeout;
};
static struct dfu_data_t dfu_data = {
	.state = appIDLE,
	.status = statusOK,
	.flash_area_id = DOWNLOAD_FLASH_AREA_ID,
	.alt_setting = 0,
	.bwPollTimeout = 0,
};
static struct dfu_worker_data_t dfu_data_worker;
static struct k_work dfu_work;
static void dfu_work_handler(struct k_work *item);
#endif 
#ifndef USE_VIA_USAGE_PAGE
int usb_send_user_if_data(uint8_t id, uint8_t *data,uint8_t len)
{
	uint8_t buf[33]={0};
	buf[0]=id;
	memcpy(buf+1,data,len);
	// LOG_HEXDUMP_DBG(buf,33,"tx");
	return zmk_usb_hid_send_report(buf,sizeof(buf));

}
#else
int usb_send_user_if_data(uint8_t id, uint8_t *data,uint8_t len)
{
	uint8_t buf[32]={0};
	buf[0]=0x03;
	memcpy(buf+1,data,len>31?31:len);
	return zmk_usb_hid_via_send(buf,sizeof(buf));
}
int usb_send_user_if_data_1(uint8_t id, uint8_t *data,uint8_t len)
{
	uint8_t buf[32]={0};

	memcpy(buf,data,len);
	return zmk_usb_hid_via_send(buf,sizeof(buf));
}
#endif 
#if EN_ZEPHYR_DFU
static void dfu_reset_counters(void)
{
	dfu_data.bytes_sent = 0U;
	dfu_data.block_nr = 0U;
	dfu_data.state = dfuIDLE;
	if (flash_img_init(&dfu_data.ctx)) {
		LOG_ERR("flash img init error");
		dfu_data.state = dfuERROR;
		dfu_data.status = errUNKNOWN;
	}
	
	LOG_DBG(".");
}
static void dfu_flash_write(uint8_t *data, size_t len)
{
	bool flush = false;

	if (!len) {
		/* Download completed */
		flush = true;
	}

	if (flash_img_buffered_write(&dfu_data.ctx, data, len, flush)) {
		LOG_ERR("flash write error");
		dfu_data.state = dfuERROR;
		dfu_data.status = errWRITE;
		sc_dfu_enable =0;
	} else if (!len) {
		const bool should_confirm = true;//IS_ENABLED(CONFIG_USB_DFU_PERMANENT_DOWNLOAD);

		LOG_DBG("flash write done");
		dfu_data.state = dfuMANIFEST_SYNC;


		LOG_DBG("Should confirm: %d", should_confirm);
		if (boot_request_upgrade(should_confirm)) {
			dfu_data.state = dfuERROR;
			dfu_data.status = errWRITE;
		}

	} else {
		dfu_data.state = dfuDNLOAD_IDLE;
	}

	// LOG_DBG("bytes written 0x%x", flash_img_bytes_written(&dfu_data.ctx));
}
static void dfu_work_handler(struct k_work *item)
{
	ARG_UNUSED(item);

	switch (dfu_data_worker.worker_state) {
	case dfuIDLE:
/*
 * If progressive erase is enabled, then erase take place while
 * image collection, so not erase whole bank at DFU beginning
 */
#ifndef CONFIG_IMG_ERASE_PROGRESSIVELY
		if (boot_erase_img_bank(DOWNLOAD_FLASH_AREA_ID)) {
			dfu_data.state = dfuERROR;
			dfu_data.status = errERASE;
			break;
		}
#endif
	case dfuDNLOAD_IDLE:
		dfu_flash_write(dfu_data_worker.buf,
				dfu_data_worker.worker_len);
		break;
	default:
		LOG_ERR("OUT of state machine");
		break;
	}
}
#endif 
void my_scdfu_init(void)
{
	
}

static unsigned char fw_sc_upgrade_over_myhid_start(void)
{	
	sc_dfu_enable=1;
	dfu_location=0;
	sc_dfu_sq=0;
	sc_dfu_wait_reboot=0;
	Dfu_CRC_Val = 0xFFFFFFFF;
	Dfu_CRC_Val_for_XXTEA =0xFFFFFFFF;

	flash_wr_temp_idx=0;
	last_sn = 0;
	
#if (WATCH_DOG_ENABLE == 1)
	app_watchdog_close();
	app_watchdog_open(MY_DFU_WATCH_DOG_TIMEOUT_MS, RESET_ALL_EXCEPT_AON);
#endif
#if EN_ZEPHYR_DFU	
	k_work_init(&dfu_work, dfu_work_handler);
	dfu_reset_counters();
#endif 	
	return 0;
}


static void dfu_commit_temp_buff_to_flash(void)
{
#if EN_ZEPHYR_DFU	
	dfu_flash_write(NULL,0);
#endif 	
}

static unsigned char fw_sc_upgrade_over_myhid_write_to_flash(unsigned char *pdata ,unsigned char len)
{
	

#if EN_ZEPHYR_DFU
	switch (dfu_data.state) {
		case dfuIDLE:
			LOG_DBG("DFU_DNLOAD start");
			dfu_reset_counters();

			if (dfu_data.flash_area_id != DOWNLOAD_FLASH_AREA_ID) {
				dfu_data.status = errWRITE;
				dfu_data.state = dfuERROR;
				LOG_ERR("This area can not be overwritten");
				break;
			}
			LOG_INF("flash id:%d",dfu_data.flash_area_id);
			dfu_data.state = dfuDNBUSY;
			dfu_data_worker.worker_state = dfuIDLE;
			dfu_data_worker.worker_len  = len;
			memcpy(dfu_data_worker.buf, pdata, len);
			k_work_submit_to_queue(&USB_WORK_Q, &dfu_work);
			break;
		case dfuDNLOAD_IDLE:
			dfu_data.state = dfuDNBUSY;
			dfu_data_worker.worker_state = dfuDNLOAD_IDLE;
			dfu_data_worker.worker_len  = len;

			memcpy(dfu_data_worker.buf, pdata, len);
			k_work_submit_to_queue(&USB_WORK_Q, &dfu_work);
			break;
		default:
			LOG_ERR("DFU_DNLOAD wrong state %d", dfu_data.state);
			dfu_data.state = dfuERROR;
			dfu_data.status = errUNKNOWN;
			dfu_reset_counters();
			return -EINVAL;
		}

	return dfu_data.state !=dfuERROR;	
#else 
	return 0;
#endif 	
}

/*
*/
static void fw_sc_upgrade_rsp_ack(unsigned char *pdata,unsigned char len)
{
#ifdef USE_VIA_USAGE_PAGE	
	#define MAX_SIZE (32-1)
#else
	#define MAX_SIZE (32)
#endif	
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)pdata;
	unsigned short sum;

	len+=2;	//len of sum
	
	ackhdr->hdr_h = 0x55;
	ackhdr->hdr_l = 0xaa;
	ackhdr->len = len;
	ackhdr->len_n = ~len;
	ackhdr->sn = sc_dfu_sn++;

	if(sc_dfu_sn==0)
		sc_dfu_sn=1;

	sum=0;
	for(unsigned char i=0;i<(len-2);i++)
		sum+=sc_dfu_rsp_buf[i+5];
	
	pdata[5+len-2] = sum&0xff;
	pdata[5+len-1] = (sum>>8)&0xff;

	int ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID,pdata,MAX_SIZE);	



	//retry again 
	if(ret)
		{

		ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID,pdata,MAX_SIZE);
	}

	if(ret==0)
		{
		if((len+5)>MAX_SIZE)
			{

			#ifndef USE_VIA_USAGE_PAGE
			ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID,(pdata+MAX_SIZE),MAX_SIZE);
			#else
			ret = usb_send_user_if_data_1(MY_DFU_RPT_INPUT_ID,(pdata+MAX_SIZE),MAX_SIZE);
			#endif 
			
			if(ret)
				{
				#ifndef USE_VIA_USAGE_PAGE
				ret = FW_SC_UPGRADE_HID_REPORT_SEND(MY_DFU_RPT_INPUT_ID,(pdata+32),MAX_SIZE);
				#else
				ret=usb_send_user_if_data_1(MY_DFU_RPT_INPUT_ID,(pdata+MAX_SIZE),MAX_SIZE);
				#endif 
			}
		}
	}

	if(ret)
		{
		LOG_DBG("fw_sc_upgrade_rsp_ack fail,ret = %d",ret);
		sc_dfu_enable = 0;
	}
}

static void fw_sc_upgrade_rsp_model_info(unsigned char sn)
{
	//unsigned char rsp_idx=0;
	BT_INFORMATION_TYPE *ackpkt = (BT_INFORMATION_TYPE *)(&sc_dfu_rsp_buf[9]);
	unsigned char str_len;
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;
	
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
 	ackhdr->rsp_cmd =  TX__OTA_REPORT;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd =SCFWU_OPCODE_GET_MODEL_INFO;
	ackhdr->ack_status = 0;
	
	ackpkt->BR_LE_mode =  SUPPORT_LE|SUPPORT_RF;
	ackpkt->bluetooth_version =  0x03;
	ackpkt->command_version[0] = 0x01;
		
	//model string
	for(unsigned char i=0;i<10;i++)
		{
		ackpkt->model_number[i]=0;
		ackpkt->fw_version[i]=0;
		ackpkt->hw_version[i]=0;
	}

	str_len=sizeof(MY_FWU_STRING_NAME)-1;
	if(str_len>10)
		str_len=10;
	memcpy(((unsigned char*)(ackpkt->model_number)),MY_FWU_STRING_NAME,str_len);

	str_len=sizeof(MY_FW_VERSION)-1;
	if(str_len>10)
		str_len=10;
	memcpy(((unsigned char*)(ackpkt->fw_version)),MY_FW_VERSION,str_len);

	str_len=sizeof(MY_HW_VERSION)-1;
	if(str_len>10)
		str_len=10;
	memcpy(((unsigned char*)(ackpkt->hw_version)),MY_HW_VERSION,str_len);
	
	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,sizeof(BT_INFORMATION_TYPE)+4);
}


static void fw_sc_upgrade_rsp_dfu_version(unsigned char sn)
{
	tDFU_VERSION *ackpkt = (tDFU_VERSION *)(&sc_dfu_rsp_buf[9]);
	CMD_RSP_HDR *ackhdr=  (CMD_RSP_HDR *)sc_dfu_rsp_buf;
	
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
	ackpkt->dfu_version_l = DFU_VERSION_VALUE&0xff;
	ackpkt->dfu_version_h = (DFU_VERSION_VALUE>>8) & 0xff;
	ackpkt->encrytion_m = DFU_ENC_SUPPORT;

 	ackhdr->rsp_cmd =  TX__OTA_REPORT;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd =SCFWU_OPCODE_GET_DFU_VERSION;
	ackhdr->ack_status = 0;
	
	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,sizeof(tDFU_VERSION)+4);
}

static void fw_sc_upgrade_set_security_level(unsigned char sn,unsigned char mode)
{
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;
	
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
	if(mode<=DFU_ENC_AES128)
		sc_dfu_security_mode=mode;

#if(MY_DFU_TRACE_ENABLE)			

	LOG_DBG("sc_dfu_security_mode = %d \r\n",sc_dfu_security_mode);

#endif

 	ackhdr->rsp_cmd =  TX__OTA_REPORT;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd =SCFWU_OPCODE_SET_SC_METHOED;
	ackhdr->ack_status =(mode<=DFU_ENC_AES128)?0:1;
	
	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,5+4);
}

static void fw_sc_upgrade_ack_success(unsigned char sn,unsigned char rx_cmd)
{
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
 	ackhdr->rsp_cmd =  TX__RSP_RX_CMD_SUCCESS;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd = rx_cmd;
	ackhdr->ack_status = 0;
	
	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,1+4);
}

static void fw_sc_upgrade_ack_fail(unsigned char sn,unsigned char rx_cmd,unsigned char reason)
{
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
 	ackhdr->rsp_cmd =  TX__RSP_RX_CMD_SUCCESS;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd = rx_cmd;
	ackhdr->ack_status = reason;
	
	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,4);
}

UINT32 CRC32(UINT32 crcval, UINT8 *buf, UINT32 length)  
{  
    UINT32 crc32_table = 0;
    UINT32 bit = 0;  

    while(length--)  
    {
        crc32_table = ((crcval ^ *buf++)&0xff);  
          
        for(bit = 0; bit < 8; bit++)  
        {  
            if(crc32_table&1)  
            {  
                crc32_table = (crc32_table >> 1)^(0xEDB88320);  
            }  
            else  
            {  
                crc32_table = crc32_table >> 1;  
            }  
              
        }  
        
        crcval = (crcval >> 8)^crc32_table; 
    }
  
    return crcval;  
}  




static void fw_sc_upgrade_write_bin_handle(unsigned char sn, unsigned char *dfu_data,unsigned char len)
{
	unsigned char ret=0;
	
	if(!sc_dfu_enable)
		{
		fw_sc_upgrade_ack_fail(sn,SCFWU_OPCODE_SEND_BIN,0x01);
		return ;
	}

	if(last_sn==sn)
		{
		fw_sc_upgrade_ack_success(sn,SCFWU_OPCODE_SEND_BIN);
		return ;
	}
	
	last_sn = sn;


	if(sc_dfu_security_mode==DFU_ENC_NONE)
		{
		Dfu_CRC_Val = CRC32(Dfu_CRC_Val, dfu_data, len);
	}

	
	ret=fw_sc_upgrade_over_myhid_write_to_flash(dfu_data,len);


	if(ret==0)
		fw_sc_upgrade_ack_success(sn,SCFWU_OPCODE_SEND_BIN);
	else
		fw_sc_upgrade_ack_fail(sn,SCFWU_OPCODE_SEND_BIN,0x01);

}

static void fw_sc_upgrade_verify_crc(unsigned char sn,unsigned char *pdata ,unsigned char len)
{
	unsigned int Ciphertext_CRC32;
	unsigned int  Plaintext_CRC32;
	unsigned char crc=1;
	
	unsigned char *ackpkt = &sc_dfu_rsp_buf[9];
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;
	
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
	Ciphertext_CRC32=0;
	Ciphertext_CRC32|= (pdata[0] & 0xff);
	Ciphertext_CRC32|= ((pdata[1]<<8) & 0xff00);
	Ciphertext_CRC32|= ((pdata[2]<<16) & 0xff0000);
	Ciphertext_CRC32|= ((pdata[3]<<24) & 0xff000000);
	
	Plaintext_CRC32=0;
	Plaintext_CRC32|= (pdata[4] & 0xff);
	Plaintext_CRC32|= ((pdata[5]<<8) & 0xff00);
	Plaintext_CRC32|= ((pdata[6]<<16) & 0xff0000);
	Plaintext_CRC32|= ((pdata[7]<<24) & 0xff000000);

#if(MY_DFU_TRACE_ENABLE)			

	LOG_DBG("fw_sc_upgrade_verify_crc(%d),rx crc:%x,%x,cal crc:%x,%x\r\n",sc_dfu_security_mode,Ciphertext_CRC32,Plaintext_CRC32,Dfu_CRC_Val,Dfu_CRC_Val_for_XXTEA);

#endif

	if(sc_dfu_security_mode==DFU_ENC_NONE)
		{
		if((Ciphertext_CRC32==Plaintext_CRC32)
			&& (Ciphertext_CRC32==Dfu_CRC_Val))
			{
			crc=0;
		}
	}


 	ackhdr->rsp_cmd =  TX__OTA_REPORT;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd = SCFWU_OPCODE_VERIFY_CRC32;
	ackhdr->ack_status = 0;

	if(crc==0)
		{
		bool check_result = false;
	#if EN_ZEPHYR_DFU	
		 check_result = dfu_data.state != dfuERROR;
	#endif 	
		if(check_result==false)
			{
			crc = 2;//error
			LOG_ERR("crc:%d",crc);
		}
	}

	ackpkt[0]= crc;
	
	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,1+4);
}

static unsigned char fw_sc_upgrade_image_switch(unsigned char sn)
{

	if(sc_dfu_enable)
		sys_reboot(SYS_REBOOT_WARM);	
	return 1;
}

static void fw_sc_upgrade_build_info(unsigned char sn)
{
	CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;	
	unsigned char *str_buf = &sc_dfu_rsp_buf[9];
	unsigned char offset=0;
	
	memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
	
 	ackhdr->rsp_cmd = TX__OTA_REPORT;	//rsp type 
	ackhdr->ack_sn = sn;
	ackhdr->ack_cmd =APP_CMD_DFU_BUILD_INFO;
	ackhdr->ack_status = 0;

	memcpy(str_buf,__DATE__,sizeof(__DATE__));
	str_buf[sizeof(__DATE__)-1]=' ';
	offset += sizeof(__DATE__);
	memcpy(str_buf+sizeof(__DATE__),__TIME__,sizeof(__TIME__));
	offset += sizeof(__TIME__);

	fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,offset+4);
}


void my_scdfu_data_handle(unsigned char *pdata,unsigned char rxlen)
{
	unsigned short hdr = 0;
	unsigned char len = pdata[2];
	unsigned char len_n = (~pdata[3]);
	unsigned char sn = pdata[4];

	hdr= pdata[1];
	hdr |= pdata[0]<<8;
	
	if((hdr == SC_FWU_HEADER)||(hdr == SC_FWU_HEADER_ACK))
		{
		if((len == len_n) && (len >2))
			{
			unsigned short sum = 0;
			unsigned short sum_rx=0;
			unsigned char len_of_dfu = len -2;
			unsigned char op_code = pdata[5];
			
			for(unsigned char i=0;i<len_of_dfu;i++ )
				sum += pdata[5+i];

			sum_rx = pdata[5+len_of_dfu];
			sum_rx |= (pdata[5+len_of_dfu+1] << 8);

			if(sum_rx == sum )
				{

				switch(op_code)
					{
					case SCFWU_OPCODE_GET_MODEL_INFO:
						fw_sc_upgrade_rsp_model_info(sn);
						break;
					case SCFWU_OPCODE_GET_DFU_VERSION:
						fw_sc_upgrade_rsp_dfu_version(sn);
						break;
					case SCFWU_OPCODE_SET_SC_METHOED:
							{
							fw_sc_upgrade_set_security_level(sn,pdata[6]);
						}
						break;
					case SCFWU_OPCODE_START:
						if(hdr == SC_FWU_HEADER_ACK)
							{
							if(fw_sc_upgrade_over_myhid_start()==0)
								fw_sc_upgrade_ack_success(sn,SCFWU_OPCODE_START);
							else
								fw_sc_upgrade_ack_fail(sn,SCFWU_OPCODE_START,0x01);
						}
						break;
					case SCFWU_OPCODE_SEND_BIN:
							fw_sc_upgrade_write_bin_handle(sn,pdata+6,len-3);
						break;
					case SCFWU_OPCODE_VERIFY_CRC32:

						dfu_commit_temp_buff_to_flash();
						
						fw_sc_upgrade_verify_crc(sn,pdata+6,len-3);
						break;
					case SCFWU_OPCODE_IMAGE_SWITCH:
						fw_sc_upgrade_image_switch(sn);
						break;
					case APP_CMD_DFU_BUILD_INFO:
						fw_sc_upgrade_build_info(sn);
						break;
					//make complier happly!!
					case 0xf2:
						{
							unsigned char *ackpkt = &sc_dfu_rsp_buf[9];
							CMD_RSP_HDR *ackhdr=(CMD_RSP_HDR *)sc_dfu_rsp_buf;
							unsigned char *buf = my_dfu_get_prv_data();
							unsigned char len = my_dfu_get_prv_len();
							
							memset(sc_dfu_rsp_buf,0,sizeof(sc_dfu_rsp_buf));
							
							ackhdr->rsp_cmd =  TX__OTA_REPORT;	//rsp type 
							ackhdr->ack_sn = sn;
							ackhdr->ack_cmd = 0xf2;
							ackhdr->ack_status = 0;

							if(len>50)
								len=50;
							
							for(unsigned char i=0;i<len;i++)
								{
								ackpkt[i]=buf[i];
							}
							
							fw_sc_upgrade_rsp_ack(sc_dfu_rsp_buf,len+4);
						}
						break;
					default:;break;
				}
			}
		}
	}
}

unsigned char my_scdfu_is_active(void)
{
	return sc_dfu_enable;
}
