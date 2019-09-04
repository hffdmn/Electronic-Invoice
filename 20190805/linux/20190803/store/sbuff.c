#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sbuff.h"
#include "utils.h"
#include "packet.h"
#include "debug.h"
#include "cJSON.h"
#include "err.h"

/**
 * 函数功能：在缓冲区中按格式构造存储数据包
 * 输入：
 *     @pcBuff[in]：数据缓冲区
 *     pucInvoiceID[in]：发票ID
 *     @pucData[in]：发票数据，json格式或者二进制格式
 *     @uiDataLen[in]：数据长度
 *     @ucCmd[in]：存储操作类型
 * 输出：数据长度
 */
int SEC_storage_api_store_buff(char *pcBuff, unsigned char *pucInvoiceID, 
				unsigned char *pucData, unsigned int uiDataLen, unsigned char ucCmd) { 

    unsigned int uiTotalLen;	// 缓冲区中数据的总长度
    unsigned int uiLen;			// 用于计算字符串长度
    struct SEC_Header stHeader; // 公共包头
    struct SEC_TLV stTlv;		// TLV结构

    // 初始化数据，为数据包头赋值
    memset(&stHeader, 0, sizeof(struct SEC_Header));
    memset(&stTlv, 0, sizeof(struct SEC_TLV));
    stHeader.ucVersion = CUR_VERSION;  //版本号 
    stHeader.ucCmd = ucCmd;  // 操作类型
    uiTotalLen = 0;

    // 跳过包头
    uiTotalLen += sizeof(struct SEC_Header);

    // 发票ID字段
    uiLen = strlen(pucInvoiceID);
    stTlv.ucType = FIELD_ID;
    stTlv.uiLen = uiLen;
    memcpy(pcBuff + uiTotalLen, &stTlv, sizeof(struct SEC_TLV));
    uiTotalLen += sizeof(struct SEC_TLV);
    memcpy(pcBuff + uiTotalLen, pucInvoiceID, uiLen);
    uiTotalLen += uiLen;

	// 数据部分
   	uiLen = uiDataLen;
	if(ucCmd == CMD_STORE_PDF || ucCmd == CMD_STORE_IMG || ucCmd == CMD_STORE_OFD) {
		stTlv.ucType = FIELD_DATA;
	} else if(ucCmd == CMD_STORE_JSON) {
		stTlv.ucType = FIELD_JSON;
	}
   	stTlv.uiLen = uiLen;
   	memcpy(pcBuff + uiTotalLen, &stTlv, sizeof(struct SEC_TLV));
   	uiTotalLen += sizeof(struct SEC_TLV);
   	memcpy(pcBuff + uiTotalLen, pucData, uiLen);
   	uiTotalLen += uiLen;

    // tlv结束标识
    stTlv.ucType = FIELD_EOF;
    stTlv.uiLen = 0;
    memcpy(pcBuff + uiTotalLen, &stTlv, sizeof(struct SEC_TLV));
    uiTotalLen += sizeof(struct SEC_TLV);

    // 数据部分长度
    uiLen = uiTotalLen - sizeof(struct SEC_Header);
    stHeader.uiLen = uiLen;

    // 将头部复制到缓冲区
    memcpy(pcBuff, &stHeader, sizeof(struct SEC_Header));  
    return uiTotalLen;
}

/**
 * 函数功能：在缓冲区中按格式构造更新数据包
 * 输入：
 *     @pcBuff[in]：数据缓冲区
 *     @pucInvoiceID[in]：发票ID
 *     @pstJsonData[in]：json数据
 *     @ucCmd[in]：更新操作类型
 * 输出：数据长度
 */
int SEC_storage_api_update_buff(char *pcBuff, 
			unsigned char *pucInvoiceID, unsigned char *pucJsonData, unsigned char ucCmd) {

    unsigned short usTotalLen;	// 缓冲区中数据的总长度
    unsigned short usLen;		// 用于计算字符串长度
    struct SEC_Header stHeader;	// 公共包头
    struct SEC_TLV stTlv;		// TLV结构

    // 初始化数据，为数据包头赋值
    memset(&stHeader, 0, sizeof(struct SEC_Header));
    memset(&stTlv, 0, sizeof(struct SEC_TLV));
    stHeader.ucVersion = CUR_VERSION;  //版本号 
	stHeader.ucCmd = ucCmd;  // 操作类型
    usTotalLen = 0;

    // 跳过包头
    usTotalLen += sizeof(struct SEC_Header);

    // 发票ID字段
    usLen = strlen(pucInvoiceID);
    stTlv.ucType = FIELD_ID;
    stTlv.uiLen = usLen;
    memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
    usTotalLen += sizeof(struct SEC_TLV);
    memcpy(pcBuff + usTotalLen, pucInvoiceID, usLen);
    usTotalLen += usLen;

	// json数据 
    usLen = strlen(pucJsonData);
    stTlv.ucType = FIELD_JSON;
    stTlv.uiLen = usLen;
    memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
    usTotalLen += sizeof(struct SEC_TLV);
    memcpy(pcBuff + usTotalLen, pucJsonData, usLen);
    usTotalLen += usLen;

    // tlv结束标识
    stTlv.ucType = FIELD_EOF;
    stTlv.uiLen = 0;
    memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
    usTotalLen += sizeof(struct SEC_TLV);

    // 数据部分长度
    usLen = usTotalLen - sizeof(struct SEC_Header);
    stHeader.uiLen = usLen;

    // 将头部复制到缓冲区
    memcpy(pcBuff, &stHeader, sizeof(struct SEC_Header));  

    return usTotalLen;
}
