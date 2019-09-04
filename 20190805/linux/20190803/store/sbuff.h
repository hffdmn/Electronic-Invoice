#ifndef _SBUFF_H
#define _SBUFF_H

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
int SEC_storage_api_store_buff(char *pcBuff, 
	unsigned char *pucInvoiceID, unsigned char *pucData, unsigned int uiDataLen, unsigned char ucCmd);

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
	unsigned char *pucInvoiceID, unsigned char *pucJsonData, unsigned char ucCmd);

#endif
