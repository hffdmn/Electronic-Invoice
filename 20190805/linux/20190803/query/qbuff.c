#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
	#include <sys/time.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
#elif defined(_WIN32)
	#include <sys/timeb.h>
	#include <winsock2.h>
	#pragma comment(lib, "Ws2_32.lib")
#endif

#include "utils.h"
#include "packet.h"
#include "debug.h"
#include "err.h"
#include "qbuff.h"

// 缓冲区大小
#define MAX_BUFF_LEN (4 * 1024 * 1024)

// 返回结果
#define SUCCESS 0
#define FAILURE 1

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucInvoiceID[in]：发票ID
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_invoiceId_query_buff(char *pcBuff, unsigned char *pucInvoiceID) {

    unsigned short usTotalLen;		// 统计包的长度，不包括包头
    unsigned short usLen;			// 统计各个字段的长度
	struct SEC_Header stHeader;		// 数据头部
	struct SEC_TLV stTlv;			// TLV结构

	// 初始化各选项
	usTotalLen = 0;
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memset(&stTlv, 0, sizeof(struct SEC_TLV));
	stHeader.ucVersion = CUR_VERSION;  // 当前版本号
	stHeader.ucCmd = CMD_ID_QUERY;  // 操作类型

	// 先跳过包头部分，之后再填充
    usTotalLen += sizeof(struct SEC_Header);

	// 包中name字段部分，包括数据长度和数据
	usLen = strlen(pucInvoiceID);
	stTlv.ucType = FIELD_ID;
	stTlv.uiLen = usLen;
	memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
	usTotalLen += sizeof(struct SEC_TLV);
	memcpy(pcBuff + usTotalLen, pucInvoiceID, usLen);
	usTotalLen += usLen;

	// TLV结束标识
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

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucQueryJson[in]：多个invoiceId组成的json字符串
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_invoiceId_batch_query_buff(char *pcBuff, unsigned char *pucQueryJson){
	unsigned short usTotalLen;	// 统计包的长度，不包括包头
    unsigned short usLen;		// 统计各个字段的长度
	struct SEC_Header stHeader;	// 数据头部
	struct SEC_TLV stTlv;		// TLV结构

	// 初始化各选项
	usTotalLen = 0;
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memset(&stTlv, 0, sizeof(struct SEC_TLV));
	stHeader.ucVersion = CUR_VERSION;
	stHeader.ucCmd = CMD_IDBATCH_QUERY;

	// 先跳过包头部分，之后再填充
    usTotalLen += sizeof(struct SEC_Header);

	// json数据部分
	usLen = strlen(pucQueryJson);
	stTlv.ucType = FIELD_JSON;
	stTlv.uiLen = usLen;
	memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
	usTotalLen += sizeof(struct SEC_TLV);
	memcpy(pcBuff + usTotalLen, pucQueryJson, usLen);
	usTotalLen += usLen;

	// TLV结束标识
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


/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucQueryJson[in]：发票要素组成的json字符串
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_elem_query_buff(char *pcBuff, unsigned char *pucQueryJson) {

    unsigned short usTotalLen;	// 统计包的长度，不包括包头
    unsigned short usLen;		// 统计各个字段的长度
	struct SEC_Header stHeader;	// 数据头部
	struct SEC_TLV stTlv;		// TLV结构

	// 初始化各选项
	usTotalLen = 0;
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memset(&stTlv, 0, sizeof(struct SEC_TLV));
	stHeader.ucVersion = CUR_VERSION;
	stHeader.ucCmd = CMD_ELEM_QUERY;

	// 先跳过包头部分，之后再填充
    usTotalLen += sizeof(struct SEC_Header);

	// json数据部分
	usLen = strlen(pucQueryJson);
	stTlv.ucType = FIELD_JSON;
	stTlv.uiLen = usLen;
	memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
	usTotalLen += sizeof(struct SEC_TLV);
	memcpy(pcBuff + usTotalLen, pucQueryJson, usLen);
	usTotalLen += usLen;

	// TLV结束标识
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

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucQueryJson[in]：发票普通字段组成的json字符串
 *     @iNum[in]：梅页结果数
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_normal_query_buff(char *pcBuff, unsigned char *pucQueryJson, int iNum) {

    unsigned short usTotalLen;	// 统计包的长度，不包括包头
    unsigned short usLen;		// 统计各个字段的长度
	struct SEC_Header stHeader;	// 数据头部
	struct SEC_TLV stTlv;		// TLV结构

	// 初始化各选项
	usTotalLen = 0;
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memset(&stTlv, 0, sizeof(struct SEC_TLV));
	stHeader.ucVersion = CUR_VERSION;  // 当前版本号
	stHeader.ucCmd = CMD_NORMAL_QUERY;  // 操作类型

	// 先跳过包头部分，之后再填充
    usTotalLen += sizeof(struct SEC_Header);

	// json部分
	usLen = strlen(pucQueryJson);
	stTlv.ucType = FIELD_JSON;
	stTlv.uiLen = usLen;
	memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
	usTotalLen += sizeof(struct SEC_TLV);
	memcpy(pcBuff + usTotalLen, pucQueryJson, usLen);
	usTotalLen += usLen;

	// 每页结果数
	usLen = sizeof(int);
	stTlv.ucType = FIELD_LIMIT_NUM;
	stTlv.uiLen = usLen;
	memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
	usTotalLen += sizeof(struct SEC_TLV);
	memcpy(pcBuff + usTotalLen, &iNum, usLen);
	usTotalLen += usLen;

	// TLV结束标识
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

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucInvoiceID[in]：发票ID
 *     @ucCmd[in]：下载操作类型
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_download_buff(char *pcBuff, unsigned char *pucInvoiceID, unsigned char ucCmd) {

    unsigned short usTotalLen;		// 统计包的长度，不包括包头
    unsigned short usLen;			// 统计各个字段的长度
	struct SEC_Header stHeader;		// 数据头部
	struct SEC_TLV stTlv;			// TLV结构

	// 初始化各选项
	usTotalLen = 0;
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memset(&stTlv, 0, sizeof(struct SEC_TLV));
	stHeader.ucVersion = CUR_VERSION;
	stHeader.ucCmd = ucCmd;

	// 先跳过包头部分，之后再填充
    usTotalLen += sizeof(struct SEC_Header);

	// 发票ID部分
	usLen = strlen(pucInvoiceID);
	stTlv.ucType = FIELD_ID;
	stTlv.uiLen = usLen;
	memcpy(pcBuff + usTotalLen, &stTlv, sizeof(struct SEC_TLV));
	usTotalLen += sizeof(struct SEC_TLV);
	memcpy(pcBuff + usTotalLen, pucInvoiceID, usLen);
	usTotalLen += usLen;

	// TLV结束标识
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
