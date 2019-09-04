#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 根据不同的平台引入对应的头文件
#if defined(__linux__)
	#include <unistd.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
#elif defined(_WIN32)
	#include <winsock2.h>
	#pragma comment(lib, "Ws2_32.lib")
#endif

#include "store.h"
#include "utils.h"
#include "packet.h"
#include "debug.h"
#include "err.h"
#include "cJSON.h"
#include "iniparser.h"
#include "converge.h"
#include "sbuff.h"
#include "parse.h"

// 文本行大小
#define MAX_LINE 1024
// 缓冲区大小
#define MAX_BUFF_LEN (4 * 1024 * 1024)

// 函数返回结果
#define SUCCESS 0
#define FAILURE 1

/**
 * 函数功能：存储发票数据，包括json形式和二进制形式
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：pdf文件二进制数据
 *      @uiFileLen[in]：pdf数据长度
 *      @ucCmd[in]：请求操作的类型
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
static 
int SEC_storage_api_store(void *phHandle, unsigned char *pucInvoiceID, unsigned char *pucData, 
		unsigned int uiDataLen, unsigned char ucCmd, char *pcGlobalID, char *pcParentID, char **pcReID) {

	unsigned char *pucBuff;					// 存放数据的缓冲区
	char acErrDesc[MAX_LINE];				// 存放错误信息的缓冲区
    int iIndex;								// 哈希取模后的结果 
    int iLen;								// 数据长度
    int iTotal;								// 记录从socket接收的数据总长度
    unsigned int uiHash;					// 哈希计算结果
    struct SEC_Header *pstHeader;			// 数据包头部
    struct SEC_StorageAPIHandle *pstHandle;	// 连接句柄
	int iResult;							// 操作结果
	cJSON *body;							// 异常日志中的body结构
	
	iResult = FAILURE;
	// 分配内存，并且清零
	pucBuff = (unsigned char*)malloc(MAX_BUFF_LEN);
	if(pucBuff == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;

	}
    memset(pucBuff, 0, MAX_BUFF_LEN);
    memset(acErrDesc, 0, MAX_LINE); 

	if(pcGlobalID != NULL) {
		*pcReID = SEC_storage_api_randomID();
		body = cJSON_CreateObject();
		cJSON_AddStringToObject(body, "invocieId", pucInvoiceID);
	}
    pstHandle = (struct SEC_StorageAPIHandle*)phHandle;
	// 在缓冲区中按照格式构造数据包
	iLen = SEC_storage_api_store_buff(pucBuff, pucInvoiceID, pucData, uiDataLen, ucCmd);
	// 进行哈希计算和取模计算
    uiHash = SEC_storage_api_jHash(pucInvoiceID);
    iIndex = uiHash % pstHandle->iGroupNum;

    // 为了防止存储到同一个节点，采用轮询的方式
    for(; ;) {
        pstHandle->pstGroup[iIndex].uiCounter++;
        if(pstHandle->pstGroup[iIndex].pucStatus[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum] == 1) {
            // 发送存储请求，返FAILURE说明socket出错
            if(SEC_storage_api_sendEx(pstHandle->pstGroup[iIndex].piSockFd[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum], pucBuff, iLen) != SUCCESS) {
				printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[iIndex].pstAddr[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum].sin_addr));
				free(pucBuff);
				return FAILURE;
			}
			break;
        }
    }

	// 存储请求日志
	if(pcGlobalID != NULL) {
		switch(ucCmd) {
			case CMD_STORE_PDF:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_store_pdf()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_STORE_IMG:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_store_img()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_STORE_OFD:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_store_ofd()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_STORE_JSON:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_store_json()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			default :
				break;
		}
	}

    // 接收存储请求的操作结果
    memset(pucBuff, 0, MAX_BUFF_LEN);
    iTotal = 0;

	// 判断数据头是否接收完成
	if(SEC_storage_api_recvEx(pstHandle->pstGroup[iIndex].piSockFd[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum], pucBuff, &iTotal, sizeof(struct SEC_Header)) != SUCCESS) {
		printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[iIndex].pstAddr[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum].sin_addr));
		free(pucBuff);
		return FAILURE;
	}

    // 解析数据包头
    pstHeader = (struct SEC_Header*)pucBuff;
    if(pstHeader->ucCmdRes == SUCCESS) { // 操作成功
		iResult = SUCCESS;
	} else if(pstHeader->ucCmdRes == FAILURE) { // 操作失败，获取失败原因描述

		if(SEC_storage_api_recvEx(pstHandle->pstGroup[iIndex].piSockFd[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum], pucBuff, &iTotal, sizeof(struct SEC_Header) + pstHeader->uiLen) != SUCCESS) {
			printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[iIndex].pstAddr[pstHandle->pstGroup[iIndex].uiCounter % pstHandle->pstGroup[iIndex].iDeviceNum].sin_addr));
			free(pucBuff);
			return FAILURE;
		}
		// 获取失败原因描述，释放内存
		memcpy(acErrDesc, pucBuff + sizeof(struct SEC_Header), pstHeader->uiLen);
		printf("Error: %s\n", acErrDesc);
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		switch(ucCmd) {
			case CMD_STORE_PDF:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_store_pdf()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_STORE_IMG:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_store_img()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_STORE_OFD:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_store_ofd()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_STORE_JSON:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_store_json()", body, acErrDesc, LOG_RESPONSE);
				break;
			default :
				break;
		}
	}

	free(pucBuff);
	return iResult;
}

/**
 * 函数功能：存储pdf格式发票数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：pdf文件二进制数据
 *      @uiFileLen[in]：pdf数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_pdf(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucFileBuff, unsigned int uiFileLen, char *pcGlobalID, char *pcParentID, char **pcReID) {
	// 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || pucFileBuff == NULL || uiFileLen <= 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.\n");
	    return ERROR_INVALID_PARAM;
	}

	return SEC_storage_api_store(phHandle, pucInvoiceID, pucFileBuff, uiFileLen, CMD_STORE_PDF, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：存储备注图片数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：备注图片二进制数据
 *      @uiFileLen[in]：备注图片数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_img(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucFileBuff, unsigned int uiFileLen, char *pcGlobalID, char *pcParentID, char **pcReID) {
    // 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || pucFileBuff == NULL || uiFileLen <= 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.\n");
		return ERROR_INVALID_PARAM;
	}

	return SEC_storage_api_store(phHandle, pucInvoiceID, pucFileBuff, uiFileLen, CMD_STORE_IMG, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：存储ofd格式发票数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：ofd文件二进制数据
 *      @uiFileLen[in]：ofd数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_ofd(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucFileBuff, unsigned int uiFileLen, char *pcGlobalID, char *pcParentID, char **pcReID) {
    // 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || pucFileBuff == NULL || uiFileLen <= 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.\n");
		return ERROR_INVALID_PARAM;
	}

	return SEC_storage_api_store(phHandle, pucInvoiceID, pucFileBuff, uiFileLen, CMD_STORE_OFD, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：存储json格式发票数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的发票数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_json(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData,	char *pcGlobalID, char *pcParentID, char **pcReID) {

	cJSON *json;

    // 检验参数有效性
    if(phHandle == NULL || pucInvoiceID == NULL || pucJsonData == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid Parameter.\n");
        return ERROR_INVALID_PARAM;
    }

	json = cJSON_Parse(pucJsonData);
    if(json == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Json Parse Error.\n");
        return ERROR_INVALID_PARAM;
    }
	cJSON_Delete(json);

	return SEC_storage_api_store(phHandle, pucInvoiceID, pucJsonData, strlen(pucJsonData), CMD_STORE_JSON, pcGlobalID, pcParentID, pcReID);
}


/**
 * 函数功能：更新发票信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @ucCmd[in]：更新操作类型
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
static 
int SEC_storage_api_update(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData,	unsigned char ucCmd, char *pcGlobalID, char *pcParentID, char **pcReID) {

	unsigned char *pucBuff;						// 存放数据的缓冲区
    int iIndex;									// 取模运算结果
    int iLen;									// 数据长度
    int iTotal;									// 已经接收的数据长度
    unsigned int uiHash;						// 哈希计算结果
    struct SEC_Header *pstHeader;				// 数据包头部
    struct SEC_StorageAPIHandle *pstHandle;		// 连接句柄
	int iResult;								// 更新操作结果
	int i;										// 循环标记
	char acErrDesc[MAX_LINE];					// 存放错误原因描述的缓冲区
	cJSON *body;								// 异常日志中的body结构

	iResult = FAILURE;
	// 分配内存，并且清零
	pucBuff = (unsigned char*)malloc(MAX_BUFF_LEN);
	if(pucBuff == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
    memset(pucBuff, 0, MAX_BUFF_LEN);

    memset(acErrDesc, 0, MAX_LINE);
	if(pcGlobalID != NULL) {
		*pcReID = SEC_storage_api_randomID();
		body = cJSON_CreateObject();
		cJSON_AddStringToObject(body, "invoiceId", pucInvoiceID);
	}
    pstHandle = (struct SEC_StorageAPIHandle*)phHandle;
	// 在缓冲区中按照格式构造数据包
    iLen = SEC_storage_api_update_buff(pucBuff, pucInvoiceID, pucJsonData, ucCmd);
	// 进行哈希计算和取模计算
    uiHash = SEC_storage_api_jHash(pucInvoiceID);  // 计算索引值
    iIndex = uiHash % pstHandle->iGroupNum;

    // 将更新请求发送到设备组中的所有节点
    for(i = 0; i < pstHandle->pstGroup[iIndex].iDeviceNum; i++) {
        if(SEC_storage_api_sendEx(pstHandle->pstGroup[iIndex].piSockFd[i], pucBuff, iLen) == FAILURE) {
			printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[iIndex].pstAddr[i].sin_addr));
		}
    }
	
	// 存储请求日志
	if(pcGlobalID != NULL) {
		switch(ucCmd) {
			case CMD_STATUS_UPDATE:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_update_status()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_OWNERSHIP_UPDATE:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_update_ownership()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_REIM_UPDATE:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_update_reim()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			default :
				break;
		}
	}

	for(i = 0; i < pstHandle->pstGroup[iIndex].iDeviceNum; i++) {
		
		// 接收存储请求的结果
		memset(pucBuff, 0, MAX_BUFF_LEN);
	    iTotal = 0;

		// 判断数据包头是否接收完成
		if(SEC_storage_api_recvEx(pstHandle->pstGroup[iIndex].piSockFd[i], pucBuff, &iTotal, sizeof(struct SEC_Header)) != SUCCESS) {
			printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[iIndex].pstAddr[i].sin_addr));
			free(pucBuff);
			return FAILURE;
		}


	    // 解析操作结果
		pstHeader = (struct SEC_Header*)pucBuff;
		if(pstHeader->ucCmdRes == SUCCESS) {
			iResult = SUCCESS;
		} else if(pstHeader->ucCmdRes == FAILURE && pstHeader->uiLen > 0) {

			if(SEC_storage_api_recvEx(pstHandle->pstGroup[iIndex].piSockFd[i], pucBuff, &iTotal, sizeof(struct SEC_Header) + pstHeader->uiLen) != SUCCESS) {
				printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[iIndex].pstAddr[i].sin_addr));
				free(pucBuff);
				return FAILURE;
			}
			memcpy(acErrDesc, pucBuff + sizeof(struct SEC_Header), pstHeader->uiLen);
		} 
	}
	// 打印错误原因
	if(iResult == FAILURE && *acErrDesc != '\0') {
		printf("Error: %s\n", acErrDesc);
	} else if(iResult == FAILURE && *acErrDesc == '\0') {
		printf("Error: No line matched.\n");
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		switch(ucCmd) {
			case CMD_STATUS_UPDATE:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_update_status()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_OWNERSHIP_UPDATE:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_update_ownership()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_REIM_UPDATE:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_update_reim()", body, acErrDesc, LOG_RESPONSE);
				break;
			default :
				break;
		}
	}

	// 释放内存
	free(pucBuff);
	return iResult;
}

/**
 * 函数功能：更新发票状态信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_update_status(void *phHandle, unsigned char *pucInvoiceID,
			unsigned char *pucJsonData,	char *pcGlobalID, char *pcParentID, char **pcReID) {

	cJSON *json;

    // 检验参数有效性
    if(phHandle == NULL || pucInvoiceID == NULL || pucJsonData == NULL || strlen(pucJsonData) == 0) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid Parameter.\n");
        return ERROR_INVALID_PARAM;
    }

	json = cJSON_Parse(pucJsonData);
    if(json == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Json Parse Error.\n");
        return ERROR_INVALID_PARAM;
    }
	cJSON_Delete(json);

	return SEC_storage_api_update(phHandle, pucInvoiceID, pucJsonData, CMD_STATUS_UPDATE, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：更新发票所有权信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_update_ownership(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData, char *pcGlobalID, char *pcParentID, char **pcReID) {

	cJSON *json;

    // 检验参数有效性
    if(phHandle == NULL || pucInvoiceID == NULL || pucJsonData == NULL || strlen(pucJsonData) == 0) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid Parameter.\n");
        return ERROR_INVALID_PARAM;
    }

	json = cJSON_Parse(pucJsonData);
    if(json == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid Parameter.\n");
        return ERROR_INVALID_PARAM;
    }
	cJSON_Delete(json);

	return SEC_storage_api_update(phHandle, pucInvoiceID, pucJsonData, CMD_OWNERSHIP_UPDATE, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：更新发票报销信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_update_reim(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData, char *pcGlobalID, char *pcParentID, char **pcReID) {

	cJSON *json;

    // 检验参数有效性
    if(phHandle == NULL || pucInvoiceID == NULL || pucJsonData == NULL || strlen(pucJsonData) == 0) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid Parameter.\n");
        return ERROR_INVALID_PARAM;
    }

	json = cJSON_Parse(pucJsonData);
    if(json == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Json Parse Error.\n");
        return ERROR_INVALID_PARAM;
    }
	cJSON_Delete(json);

	return SEC_storage_api_update(phHandle, pucInvoiceID, pucJsonData, CMD_REIM_UPDATE, pcGlobalID, pcParentID, pcReID);
}
