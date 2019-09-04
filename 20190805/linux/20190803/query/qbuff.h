#ifndef _QBUFF_H
#define _QBUFF_H

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucInvoiceID[in]：发票ID
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_invoiceId_query_buff(char *pcBuff, unsigned char *pucInvoiceID);

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucQueryJson[in]：多个invoiceId组成的json字符串
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_invoiceId_batch_query_buff(char *pcBuff, unsigned char *pucQueryJson);

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucQueryJson[in]：发票要素组成的json字符串
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_elem_query_buff(char *pcBuff, unsigned char *pucQueryJson);

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucQueryJson[in]：发票普通字段组成的json字符串
 *     @iNum[in]：每个页结果数
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_normal_query_buff(char *pcBuff, unsigned char *pucQueryJson, int iNum);

/**
 * 函数功能：在缓冲区中按格式构造包
 * 参数：
 *     @pcBuff[out]：缓冲区，存放数据
 *     @pucInvoiceID[in]：发票ID
 *     @ucCmd[in]：下载操作类型
 * 返回值：缓冲区中数据的的长度
 */
int SEC_storage_api_download_buff(char *pcBuff, unsigned char *pucInvoiceID, unsigned char ucCmd);

#endif
