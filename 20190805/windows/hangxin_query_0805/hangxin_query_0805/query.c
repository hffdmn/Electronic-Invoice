#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
#elif defined(_WIN32)
	#include <winsock2.h>
	#pragma comment(lib, "Ws2_32.lib")
#endif

#include "utils.h"
#include "packet.h"
#include "query.h"
#include "debug.h"
#include "err.h"
#include "cJSON.h"
#include "iniparser.h"
#include "converge.h"
#include "parse.h"

// 文本行大小
#define MAX_LINE 1024
// 缓冲区大小
#define MAX_BUFF_LEN (4 * 1024 * 1024)

// 函数返回值
#define SUCCESS 0
#define FAILURE 1

// realloc时的增加单元
#define ALLOC_UNIT 128

// 请求可能会发送给多个节点，此时就需要记录该请求的状态
struct SEC_RecvStatus {
    int iFdNum;					// 记录当前请求一共发送给了多少个节点
    int iLeft;					// 记录还有多少节点没有返回数据完毕
	unsigned char ucUpdateRes;	// 记录更新操作的结果
};

/**
 * 函数功能：释放查询json数据占用的内存
 * 参数：
 *      @pucJsonData[in]：数组首地址；
 *      @piDataNum[in]：数组中元素个数
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT int SEC_storage_api_json_free(unsigned char **pucJsonData, int piDataNum) {

	int i;
	// 参数的有效性
	if(pucJsonData == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.");
        return ERROR_INVALID_PARAM;
	}
	for(i = 0; i < piDataNum; i++) {
		free(pucJsonData[i]);
	}
	free(pucJsonData);
	return SUCCESS;
}

/**
 * 函数功能：释放文件数据占用的内存
 * 参数：
 *      @pucBuff[in]：数组首地址；
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT int SEC_storage_api_buff_free(unsigned char *pucBuff) {

	free(pucBuff);
	return SUCCESS;
}

/**
 * 函数功能：执行查询，包括ID查询、四要素查询、一般条件查询
 * 参数：
 *     @iSockFd[in]：连接服务器的文件描述符
 *     @pcBuff[in]：接受查询结果的缓冲区
 *     @pstData[out]：一级指针地址，指针指向元数据数组
 *     @piDataNum[out]]：查询结果数
 *     @piSize[in & out]：结果数据的最大长度
 *     @puiCount[out]：符合分页查询的总结果数
 *     @pcErrDesc[out]：错误原因描述字符串
 * 返回值：成功返回SUCCESS；失败返回FAILURE
 */
static
int SEC_storage_api_query(int iSockFd, unsigned char *pucBuff, unsigned char ***pstData, 
		int *piDataNum, int *piSize, unsigned int *puiCount, unsigned char *pcErrDesc) {

	struct SEC_Header *pstHeader;	// 数据包头部
	int iLeft;						// 缓冲区内剩余的数据长度
	int iLen;						// 接收数据长度
	int iOffset;					// 缓冲区中的偏移位置
	unsigned int uiCount;			// 记录每个节点分页查询的结果数

	// 初始化变量
	iLeft = 0;
	uiCount = 0;

	while(1) {
		// 接收数据
		iLen = recv(iSockFd, pucBuff + iLeft, MAX_BUFF_LEN - iLeft, 0);

		if(iLen > 0) {		// 收到了数据
			iLeft += iLen;  // 更新iLeft值
		} else {			// socket出现错误
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "recv()");
			return FAILURE;
		}

		iOffset = 0;
		while(iLeft >= sizeof(struct SEC_Header)) { // 判断数据够不够一个包头

			pstHeader = (struct SEC_Header*)(pucBuff + iOffset);  // 解析数据包头

			// 判断够不够一个完整的作业包
			if(iLeft - sizeof(struct SEC_Header) < pstHeader->uiLen) {
				// 不够一个数据包，直接跳出循环
				break;
			} else {

				if(pstHeader->ucCmdRes == FAILURE) {  // 该socket本次传输的最后一个包，这里不是按多路复用的方式写的，所以可以直接返回
					memcpy(pcErrDesc, pucBuff + iOffset + sizeof(struct SEC_Header), pstHeader->uiLen);
					return FAILURE;
				}
				if(pstHeader->ucIsLast == 1) {  // 该socket本次传输的最后一个包，这里不是按多路复用的方式写的，所以可以直接返回
					return SUCCESS;
				}

				if(pstHeader->ucCmd == CMD_NORMAL_COUNT) {
					sscanf(pucBuff + iOffset + sizeof(struct SEC_Header), "%u", &uiCount);
					*puiCount += uiCount;
					// 更新偏移量
					iOffset = iOffset + sizeof(struct SEC_Header) + pstHeader->uiLen;  // 计算作业包的总长度
					iLeft = iLeft - sizeof(struct SEC_Header) - pstHeader->uiLen; 
					continue;
				}

				// 扩充内存
				if(*piDataNum >= *piSize) {
					// 20190411
					*pstData = (unsigned char**)realloc(*pstData, (*piSize + ALLOC_UNIT) * sizeof(unsigned char*));
					*piSize = *piSize + ALLOC_UNIT;
				}
				
				// 解析数据包
				(*pstData)[*piDataNum] = (unsigned char*)malloc(sizeof(unsigned char) * (pstHeader->uiLen + 1));
				memset((*pstData)[*piDataNum], 0, pstHeader->uiLen + 1);
				memcpy((*pstData)[*piDataNum], pucBuff + iOffset + sizeof(struct SEC_Header), pstHeader->uiLen);

				(*piDataNum)++;
				// 更新偏移量
				iOffset = iOffset + sizeof(struct SEC_Header) + pstHeader->uiLen;  // 计算作业包的总长度
				iLeft = iLeft - sizeof(struct SEC_Header) - pstHeader->uiLen; 
			}
		}
		if(iLeft > 0) {
			// 将剩余数据移动到缓冲区起始位置
			memmove(pucBuff, pucBuff + iOffset, iLeft);
		}
	}
	return SUCCESS;	
}

/**
 * 函数功能：根据发票ID进行查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_invoiceId_query(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID) {

	unsigned char *pucBuff;					// 缓存区域
	int iLen;								// 数据长度
	struct SEC_StorageAPIHandle *pstHandle;	// 连接句柄
    unsigned int uiHash;					// 哈希计算结果
    unsigned int uiIndex;					// 取模计算结果
    int i, j;								// 循环标记
    struct SEC_RecvStatus *pstRecvStatus;	// 指向一块内存，用于记录查询的状态
    struct SEC_Header *pstHeader;			// 数据包头部
	int iSize;								// 记录申请的数组大小
	int iErrCount;							// 记录操作失败的节点数
	char acErrDesc[MAX_LINE];				// 存放错误原因描述的内存区域
	cJSON *body;							// 异常日志中对应的body结构
	int iRet;								// 记录返回结果

	// 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || strlen(pucInvoiceID) == 0 || pucJsonData == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.");
        return ERROR_INVALID_PARAM;
	}

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
	// 构造数据包
	iLen = SEC_storage_api_invoiceId_query_buff(pucBuff, pucInvoiceID);

	// 进行哈希计算和取模计算
    uiHash = SEC_storage_api_jHash(pucInvoiceID);
    uiIndex = uiHash % pstHandle->iGroupNum;

	// 因为可能会发送给多个节点，所以此时需要记录状态信息
    pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
    pstRecvStatus->iFdNum = pstHandle->pstGroup[uiIndex].iDeviceNum;
    pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
    //iErrCount = pstRecvStatus->iFdNum;
	iErrCount = 0;

    pstHeader = (struct SEC_Header*)pucBuff;
    pstHeader->pointer1 = (void*)pstRecvStatus;

    // 发送数据
    for(i = 0; i < pstHandle->pstGroup[uiIndex].iDeviceNum; i++) {
        if(SEC_storage_api_sendEx(pstHandle->pstGroup[uiIndex].piSockFd[i], pucBuff, iLen) == FAILURE) {
			printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[uiIndex].pstAddr[i].sin_addr));
		}
    }
	
	// 存储请求日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_invoiceId_query()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
	}

	// 初始化变量，分配存储查询结果的内存空间
    *piDataNum = 0;
    iSize = ALLOC_UNIT;
	*pucJsonData = (unsigned char**)malloc(sizeof(unsigned char*) * ALLOC_UNIT);
	memset(*pucJsonData, 0, sizeof(unsigned char*) * ALLOC_UNIT);
    
	// 依次从各个节点接收数据 
	for(i = 0; i < pstHandle->pstGroup[uiIndex].iDeviceNum; i++) {
        if(SEC_storage_api_query(pstHandle->pstGroup[uiIndex].piSockFd[i], pucBuff, pucJsonData, piDataNum, &iSize, NULL, acErrDesc) == FAILURE) {
			 printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[uiIndex].pstAddr[i].sin_addr));
			iErrCount++;
		}
    }

	// 打印错误信息
	if(iErrCount == pstRecvStatus->iFdNum) {
		printf("Error: %s\n", acErrDesc);
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_invoiceId_query()", body, acErrDesc, LOG_RESPONSE);
	}

	iRet = (iErrCount == pstRecvStatus->iFdNum ? FAILURE : SUCCESS);

	// 释放内存
	free(pstRecvStatus);
	free(pucBuff);
	return iRet;
}

/**
 * 函数功能：多id批量查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucQueryJson[in]：多条invoiceid组成的json字符串
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_invoiceId_batch_query(void *phHandle, unsigned char *pucQueryJson, 
		unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID){
	
	unsigned char *pucBuff;					// 缓存区域
	int iLen;								// 数据长度
	struct SEC_StorageAPIHandle *pstHandle;	// 连接句柄
	unsigned int uiHash;					// 哈希计算结果
    	unsigned int uiIndex;					// 取模计算结果
    	int i, j;								// 循环标记
    	struct SEC_RecvStatus *pstRecvStatus;	// 指向一块内存，用于记录查询的状态
    	struct SEC_Header *pstHeader;			// 数据包头部
	int iSize;								// 记录申请的数组大小
	int iErrCount;							// 记录操作失败的节点数
	char acErrDesc[MAX_LINE];				// 存放错误原因描述的内存区域
	cJSON *json;							// 解析json参数是否正确
	int iRet;								// 记录返回结果

	// 检验参数有效性
	if(phHandle == NULL || pucQueryJson == NULL || strlen(pucQueryJson) == 0 || pucJsonData == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.\n");
        return ERROR_INVALID_PARAM;
	}

	json = cJSON_Parse(pucQueryJson);
	if(json == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Json Parse Error.\n");
        return ERROR_INVALID_PARAM;
	}
	//cJSON_Delete(json);
	
	pucBuff = (unsigned char*)malloc(MAX_BUFF_LEN);
	if(pucBuff == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		cJSON_Delete(json);
        return FAILURE;
	}
	memset(pucBuff, 0, MAX_BUFF_LEN);

	memset(acErrDesc, 0, MAX_LINE);
	if(pcGlobalID != NULL) {
		*pcReID = SEC_storage_api_randomID();
	}
	pstHandle = (struct SEC_StorageAPIHandle*)phHandle;
	// 构造数据包
	iLen = SEC_storage_api_invoiceId_batch_query_buff(pucBuff, pucQueryJson);

	// 发送数据
    pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
    pstRecvStatus->iFdNum = 0;
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        pstRecvStatus->iFdNum += pstHandle->pstGroup[i].iDeviceNum;
    }

	// 因为可能会发送给多个节点，所以此时需要记录状态信息
    pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
	//iErrCount = pstRecvStatus->iFdNum;
	iErrCount = 0;

    pstHeader = (struct SEC_Header*)pucBuff;
    pstHeader->pointer1 = (void*)pstRecvStatus;

    // 发送数据
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) {
            if(SEC_storage_api_sendEx(pstHandle->pstGroup[i].piSockFd[j], pucBuff, iLen) == FAILURE) {
				printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr));
			}
        }
    }

	// 存储请求日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_invoiceId_batch_query()", cJSON_Duplicate(json, 1), acErrDesc, LOG_REQUEST);
	}

	// 初始化变量，分配存储查询结果的内存空间
    *piDataNum = 0;
    *pucJsonData = (unsigned char**)malloc(sizeof(unsigned char*) * ALLOC_UNIT);
    memset(*pucJsonData, 0, sizeof(unsigned char*) * ALLOC_UNIT);
    iSize = ALLOC_UNIT;

	// 依次从各个节点接收数据 
    for(i = 0; i < pstHandle->iGroupNum; i++) {
       for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) {
            if(SEC_storage_api_query(pstHandle->pstGroup[i].piSockFd[j], pucBuff, pucJsonData, piDataNum, &iSize, NULL, acErrDesc) == FAILURE) {
				printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr));
				iErrCount++;
			}
        }
    }

	//	打印错误信息
	if(iErrCount == pstRecvStatus->iFdNum) {
		printf("Error: %s\n", acErrDesc);
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_invoiceId_batch_query()", json, acErrDesc, LOG_RESPONSE);
	}

	// 释放内存
	if(pcGlobalID == NULL) {
		cJSON_Delete(json);
	}

	iRet = (iErrCount == pstRecvStatus->iFdNum ? FAILURE : SUCCESS);

	free(pstRecvStatus);
	free(pucBuff);
	return iRet;
}


/**
 * 函数功能：根据发票要素进行查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucQueryJson[in]：查询条件组成的json字符串
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_elem_query(void *phHandle, unsigned char *pucQueryJson, 
		unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID) {

	unsigned char *pucBuff;					// 缓存区域
	int iLen;								// 数据长度
	struct SEC_StorageAPIHandle *pstHandle;	// 连接句柄
    unsigned int uiHash;					// 哈希计算结果
    unsigned int uiIndex;					// 取模计算结果
    int i, j;								// 循环标记
    struct SEC_RecvStatus *pstRecvStatus;	// 指向一块内存，用于记录查询的状态
    struct SEC_Header *pstHeader;			// 数据包头部
	int iSize;								// 记录申请的数组大小
	int iErrCount;							// 记录操作失败的节点数
	char acErrDesc[MAX_LINE];				// 存放错误原因描述的内存区域
	cJSON *json;							// 解析json参数是否正确
	int iRet;								// 记录返回结果

	// 检验参数有效性
	if(phHandle == NULL || pucQueryJson == NULL || strlen(pucQueryJson) == 0 || pucJsonData == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.\n");
        return ERROR_INVALID_PARAM;
	}

	json = cJSON_Parse(pucQueryJson);
	if(json == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Json Parse Error.\n");
        return ERROR_INVALID_PARAM;
	}
	//cJSON_Delete(json);
	
	pucBuff = (unsigned char*)malloc(MAX_BUFF_LEN);
	if(pucBuff == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		cJSON_Delete(json);
        return FAILURE;
	}
	memset(pucBuff, 0, MAX_BUFF_LEN);

	memset(acErrDesc, 0, MAX_LINE);
	if(pcGlobalID != NULL) {
		*pcReID = SEC_storage_api_randomID();
	}
	pstHandle = (struct SEC_StorageAPIHandle*)phHandle;
	// 构造数据包
	iLen = SEC_storage_api_elem_query_buff(pucBuff, pucQueryJson);

	// 发送数据
    pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
    pstRecvStatus->iFdNum = 0;
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        pstRecvStatus->iFdNum += pstHandle->pstGroup[i].iDeviceNum;
    }

	// 因为可能会发送给多个节点，所以此时需要记录状态信息
    pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
	//iErrCount = pstRecvStatus->iFdNum;
	iErrCount = 0;

    pstHeader = (struct SEC_Header*)pucBuff;
    pstHeader->pointer1 = (void*)pstRecvStatus;

    // 发送数据
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) {
            if(SEC_storage_api_sendEx(pstHandle->pstGroup[i].piSockFd[j], pucBuff, iLen) == FAILURE) {
				printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr));
			}
        }
    }

	// 存储请求日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_elem_query()", cJSON_Duplicate(json, 1), acErrDesc, LOG_REQUEST);
	}

	// 初始化变量，分配存储查询结果的内存空间
    *piDataNum = 0;
    *pucJsonData = (unsigned char**)malloc(sizeof(unsigned char*) * ALLOC_UNIT);
	memset(*pucJsonData, 0, sizeof(unsigned char*) * ALLOC_UNIT);
    iSize = ALLOC_UNIT;

	// 依次从各个节点接收数据 
    for(i = 0; i < pstHandle->iGroupNum; i++) {
       for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) { 
            if(SEC_storage_api_query(pstHandle->pstGroup[i].piSockFd[j], pucBuff, pucJsonData, piDataNum, &iSize, NULL, acErrDesc) == FAILURE) {
				printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr));
				iErrCount++;
			}
        }
    }

	//	打印错误信息
	if(iErrCount == pstRecvStatus->iFdNum) {
		printf("Error: %s\n", acErrDesc);
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_elem_query()", json, acErrDesc, LOG_RESPONSE);
	}

	// 释放内存
	if(pcGlobalID == NULL) {
		cJSON_Delete(json);
	}

	iRet = (iErrCount == pstRecvStatus->iFdNum ? FAILURE : SUCCESS);

	free(pstRecvStatus);
	free(pucBuff);
	return iRet;
}

/**
 * 函数功能：用于快速排序函数，比较数组中两个元素的大小,
 *			使用inIssuTime和invoiceId两个字段进行排序，降序
 * 参数：
 * 		@p1[in]：p1指向其中一个元素
 * 		@p2[in]：p2指向另一个与元素
 * 返回值：比较结果，如果p1指向的元素大于p2指向的元素，返回正数；
 *		   如果p1指向的元素小于p2指向的元素，返回负数；
 *		   如果p1指向的元素等于p2指向的元素，返回0；
 */
static
int cmp(const void *p1, const void *p2) {

	cJSON *c1;		// 将p1指向的字符串变为cJSON对象
	cJSON *c2;		// 将p2指向的字符串变为cJSON对象
	char *pcInvoiceId1;
	char *pcInvoiceId2;
	char *pcTime1;
	char *pcTime2;
	int iResult;

	// 解析字符串
	c1 = cJSON_Parse(*(unsigned char**)p1);
	c2 = cJSON_Parse(*(unsigned char**)p2);
	pcTime1 = cJSON_GetStringValue(cJSON_GetObjectItem(c1, "inIssuTime"));
	pcTime2 = cJSON_GetStringValue(cJSON_GetObjectItem(c2, "inIssuTime"));
	pcInvoiceId1 = cJSON_GetStringValue(cJSON_GetObjectItem(c1, "invoiceId"));
	pcInvoiceId2 = cJSON_GetStringValue(cJSON_GetObjectItem(c2, "invoiceId"));

	// 解析json中的invoiceId并进行比较
	iResult = strcmp(pcTime2, pcTime1);

	if(iResult == 0) {
		iResult = strcmp(pcInvoiceId2, pcInvoiceId1);
	}

	// 释放内存
	cJSON_Delete(c1);
	cJSON_Delete(c2);

	// 返回结果
	return iResult;
}

/**
 * 函数功能：根据普通字段进行查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucQueryJson[in]：查询条件组成的json字符串
 *      @iOffset[in]：分页偏移地址
 *      @iNum[in]：分页结果数
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_normal_query(void *phHandle, unsigned char *pucQueryJson, int iOffset, 
		int iNum, unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID) {

	unsigned char *pucBuff;					// 缓存区域
	int iLen;								// 数据长度
	struct SEC_StorageAPIHandle *pstHandle;	// 连接句柄
    unsigned int uiHash;					// 哈希计算结果
    unsigned int uiIndex;					// 取模计算结果
    int i, j;								// 循环标记
    struct SEC_RecvStatus *pstRecvStatus;	// 指向一块内存，用于记录查询的状态
    struct SEC_Header *pstHeader;			// 数据包头部
	int iSize;								// 记录申请的数组大小
	int iErrCount;							// 记录操作失败的节点数
	char acErrDesc[MAX_LINE];				// 存放错误原因描述的内存区域
	unsigned int uiCount;					// 符合分页查询的总结果数
	int iCurPage;							// 当前页号
	int iPageSize;							// 页大小
	int iTotalPage;							// 总页数
	cJSON *root;							// 用于生成最终的json字符串
	cJSON *array;							// 用于生成json数组
	cJSON *item;							// 每一个json项
	cJSON *json;							// 解析参数json是否正确
	int iRet;								// 记录返回结果

	// 检验参数有效性
	if(phHandle == NULL || pucQueryJson == NULL || strlen(pucQueryJson) == 0 || pucJsonData == NULL || iOffset < 0 || iNum < 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.\n");
        return ERROR_INVALID_PARAM;
	}

	json = cJSON_Parse(pucQueryJson);
	if(json == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Json Parse Error.\n");
        return ERROR_INVALID_PARAM;
	}
	//cJSON_Delete(json);
	
	pucBuff = (unsigned char*)malloc(MAX_BUFF_LEN);
	if(pucBuff == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		cJSON_Delete(json);
        return FAILURE;
	}
	memset(pucBuff, 0, MAX_BUFF_LEN);
	memset(acErrDesc, 0, MAX_LINE);

	// 初始化变量
	uiCount = 0;
	iCurPage = 0;
	iPageSize = 0;
	iTotalPage = 0;
	root = NULL;
	array = NULL;
	item = NULL;
	
	if(pcGlobalID != NULL) {
		*pcReID = SEC_storage_api_randomID();
	}
	pstHandle = (struct SEC_StorageAPIHandle*)phHandle;
	// 构造数据包
	iLen = SEC_storage_api_normal_query_buff(pucBuff, pucQueryJson, iNum);

	// 因为可能会发送给多个节点，所以此时需要记录状态信息
    pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
    pstRecvStatus->iFdNum = 0;
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        pstRecvStatus->iFdNum += pstHandle->pstGroup[i].iDeviceNum;
    }
    pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
	//iErrCount = pstRecvStatus->iFdNum;
	iErrCount = 0;
    pstHeader = (struct SEC_Header*)pucBuff;
    pstHeader->pointer1 = (void*)pstRecvStatus;

	// 发送数据
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) {
            if(SEC_storage_api_sendEx(pstHandle->pstGroup[i].piSockFd[j], pucBuff, iLen) == FAILURE) {
				printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr));
			}
        }
    }

	// 存储请求日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_normal_query()", cJSON_Duplicate(json, 1), acErrDesc, LOG_REQUEST);
	}

	// 初始化变量，分配存储查询结果的内存空间
    *piDataNum = 0;
    *pucJsonData = (unsigned char**)malloc(sizeof(unsigned char*) * ALLOC_UNIT);
	memset(*pucJsonData, 0, sizeof(unsigned char*) * ALLOC_UNIT);
    iSize = ALLOC_UNIT;

	// 依次从各个节点接收数据 
    for(i = 0; i < pstHandle->iGroupNum; i++) {
        for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) { 
            if(SEC_storage_api_query(pstHandle->pstGroup[i].piSockFd[j], pucBuff, pucJsonData, piDataNum, &iSize, &uiCount, acErrDesc) == FAILURE) {
				printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr));
				iErrCount++;
			}
        }
    }

	// 根据要求的json格式输出结果
	if(iErrCount == pstRecvStatus->iFdNum) {
		printf("Error: %s\n", acErrDesc);
		*piDataNum = 0;
	} else {
		// 将结果拼接成json格式返回
		iCurPage = iOffset / iNum + 1;
		iPageSize = iNum;
		iTotalPage = ((uiCount % iNum) ?  uiCount / iNum + 1: uiCount / iNum) + iOffset / iNum;
		root = cJSON_CreateObject();
		cJSON_AddNumberToObject(root, "curPage", iCurPage);
		array = cJSON_CreateArray();
		
		// 按照关键字inIssuTime和invoiceId排序，降序
		qsort((void*)(*pucJsonData), *piDataNum, sizeof(unsigned char*), cmp);

		// 选择页大小与返回结果中的最小值
		if(*piDataNum < iNum) {
			for(i = 0; i < *piDataNum; i++) {
				item = cJSON_CreateRaw((*pucJsonData)[i]);
				cJSON_AddItemToArray(array, item);
			}
		} else {
			for(i = 0; i < iNum; i++) {
				item = cJSON_CreateRaw((*pucJsonData)[i]);
				cJSON_AddItemToArray(array, item);
			}
		}

		// 添加各字段
		cJSON_AddItemToObject(root, "data", array);
		cJSON_AddNumberToObject(root, "pageSize", iPageSize);
		cJSON_AddNumberToObject(root, "totalCount", uiCount + iOffset);
		cJSON_AddNumberToObject(root, "totalPage", iTotalPage);

		// 处理最终的输出结果
		for(i = 0; i < *piDataNum; i++) {
			free((*pucJsonData)[i]);
		}
		(*pucJsonData)[0] = cJSON_PrintUnformatted(root);
		*piDataNum = 1;
	}

	if(root != NULL) {
		cJSON_Delete(root);
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_normal_query()", json, acErrDesc, LOG_RESPONSE);
	}

	// 释放内存
	if(pcGlobalID == NULL) {
		cJSON_Delete(json);
	}

	iRet = (iErrCount == pstRecvStatus->iFdNum ? FAILURE : SUCCESS);

	// 释放内存
	free(pstRecvStatus);
	free(pucBuff);
	return iRet;
}

/**
 * 函数功能：下载文件数据
 * 参数：
 *     @iSockFd[in]：socket描述符
 *     @pcBuff[in]：数据缓冲区
 *     @pucBytes[out]：存放下载数据
 *     @puiLen[out]]：下载数据长度
 *     @pcErrDesc[out]：错误原因描述
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
static 
int SEC_storage_api_download_file(int iSockFd, unsigned char *pucBuff, 
		unsigned char **pucBytes, unsigned int *puiLen, char *pcErrDesc) {

	struct SEC_Header *pstHeader;	// 数据包头部
	int iLeft;						// 缓冲区内剩余的数据长度
	int iLen;						// 接收数据长度
	int iTotal;						// 已经解析完成的结果数
	int iOffset;					// 记录缓冲区中的偏移位置

	// 初始化变量
	iTotal = 0;
	iLeft = 0;

	while(1) {
		// 接收数据
		iLen = recv(iSockFd, pucBuff + iLeft, MAX_BUFF_LEN - iLeft, 0);

		if(iLen > 0) {		// 收到了数据
			iLeft += iLen;	// 更新iLeft值
		} else {			// socket出现错误
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "recv()");
			return FAILURE;
		}

		iOffset = 0;
		while(iLeft >= sizeof(struct SEC_Header)) { // 判断数据够不够一个包头

			pstHeader = (struct SEC_Header*)(pucBuff + iOffset);  // 解析数据包头

			// 判断够不够一个完整的作业包
			if(iLeft - sizeof(struct SEC_Header) < pstHeader->uiLen) {
				// 不够一个数据包，直接跳出循环 
				break;
			} else {

				if(pstHeader->ucCmdRes == FAILURE) {  // 该socket本次传输的最后一个包，这里不是按多路复用的方式写的，所以可以直接返回
					memcpy(pcErrDesc, pucBuff + iOffset + sizeof(struct SEC_Header), pstHeader->uiLen);
					return FAILURE;
				}
				if(pstHeader->ucIsLast == 1) {  // 该socket本次传输的最后一个包，这里不是按多路复用的方式写的，所以可以直接返回
					return SUCCESS;
				}
				// 分配内存
				if(pstHeader->uiLen > 0) {
					*pucBytes = (unsigned char*)malloc(sizeof(unsigned char) * pstHeader->uiLen);
					*puiLen = (unsigned int)pstHeader->uiLen;
					// 解析数据包
					memset(*pucBytes, 0, pstHeader->uiLen);
					memcpy(*pucBytes, pucBuff + iOffset + sizeof(struct SEC_Header), pstHeader->uiLen);
				}

				// 更新偏移量
				iOffset = iOffset + sizeof(struct SEC_Header) + pstHeader->uiLen;  // 计算作业包的总长度
				iLeft = iLeft - sizeof(struct SEC_Header) - pstHeader->uiLen; 
			}
		}
		if(iLeft > 0) {
			// 将剩余的数据移动到缓冲区起始位置
			memmove(pucBuff, pucBuff + iOffset, iLeft);
		}
	}
	return SUCCESS;	
}

/**
 * 函数功能：下载文件数据
 * 参数：
 * 		@phHandle[in]：连接句柄
 * 		@pucInvoiceID[in]：发票ID
 * 		@pucBytes[in]：存放下载到的二进制数据
 * 		@puiLen[in]：数据长度
 * 		@ucCmd[in]：下载操作类型
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
static 
int SEC_storage_api_download(void *phHandle, unsigned char *pucInvoiceID, unsigned char **pucBytes, unsigned int *puiLen, unsigned char ucCmd, char *pcGlobalID, char *pcParentID, char **pcReID) {

	unsigned char *pucBuff;					// 缓存区域
	int iLen;								// 数据长度
	struct SEC_StorageAPIHandle *pstHandle;	// 连接句柄
    unsigned int uiHash;					// 哈希计算结果
    unsigned int uiIndex;					// 取模计算结果
    int i, j;								// 循环标记
    struct SEC_RecvStatus *pstRecvStatus;	// 指向一块内存，用于记录查询的状态
    struct SEC_Header *pstHeader;			// 数据包头部
	int iErrCount;							// 记录操作失败的节点数
	char acErrDesc[MAX_LINE];				// 存放错误原因描述的内存区域
	cJSON *body;							// 异常日志中的body结构
	int iRet;								// 记录返回结果

	// 分配内存
	pucBuff = (unsigned char*)malloc(MAX_BUFF_LEN);
	if(pucBuff == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
	memset(pucBuff, 0, MAX_BUFF_LEN);

	memset(acErrDesc, 0, MAX_LINE);
	*puiLen = 0;
	if(pcGlobalID != NULL) {
		// 生成32字节随机ID
		*pcReID = SEC_storage_api_randomID();
		// 创建日志body结构及其内容
		body = cJSON_CreateObject();
		cJSON_AddStringToObject(body, "invoiceId", pucInvoiceID);
	}
	pstHandle = (struct SEC_StorageAPIHandle*)phHandle;
	// 构造数据包
	iLen = SEC_storage_api_download_buff(pucBuff, pucInvoiceID, ucCmd);

	//进行哈希计算和取模计算 
    uiHash = SEC_storage_api_jHash(pucInvoiceID);
    uiIndex = uiHash % pstHandle->iGroupNum;

	// 因为可能会发送给多个节点，所以此时需要记录状态信息
    pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
    pstRecvStatus->iFdNum = pstHandle->pstGroup[uiIndex].iDeviceNum;
    pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
	//iErrCount = pstRecvStatus->iFdNum;
	iErrCount = 0;

    pstHeader = (struct SEC_Header*)pucBuff;
    pstHeader->pointer1 = (void*)pstRecvStatus;

    // 发送数据
    for(i = 0; i < pstHandle->pstGroup[uiIndex].iDeviceNum; i++) {
        if(SEC_storage_api_sendEx(pstHandle->pstGroup[uiIndex].piSockFd[i], pucBuff, iLen) == FAILURE) {
			printf("Send to fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[uiIndex].pstAddr[i].sin_addr));
		}
    }

	// 存储请求日志
	if(pcGlobalID != NULL) {
		switch(ucCmd) {
			case CMD_DOWNLOAD_PDF:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_pdf_download()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_DOWNLOAD_IMG:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_img_download()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			case CMD_DOWNLOAD_OFD:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, pcParentID, "SEC_storage_api_ofd_download()", cJSON_Duplicate(body, 1), acErrDesc, LOG_REQUEST);
				break;
			default :
				break;
		}
	}

	// 依次从各个节点接收数据 
	memset(pucBuff, 0, MAX_BUFF_LEN);
    for(i = 0; i < pstHandle->pstGroup[uiIndex].iDeviceNum; i++) {
        if( SEC_storage_api_download_file(pstHandle->pstGroup[uiIndex].piSockFd[i], pucBuff, pucBytes, puiLen, acErrDesc) == FAILURE) {  
			printf("Recv from fir_gate %s error\n", inet_ntoa(pstHandle->pstGroup[uiIndex].pstAddr[i].sin_addr));
			iErrCount++;
		}
    }

	// 打印错误信息
	if(iErrCount == pstRecvStatus->iFdNum) {
		printf("Error: %s\n", acErrDesc);
		puiLen = 0;
	} else if(*puiLen == 0) {
		printf("Error: no matched line.\n");
	}

	// 存储响应日志
	if(pcGlobalID != NULL) {
		switch(ucCmd) {
			case CMD_DOWNLOAD_PDF:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_pdf_download()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_DOWNLOAD_IMG:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_img_download()", body, acErrDesc, LOG_RESPONSE);
				break;
			case CMD_DOWNLOAD_OFD:
				SEC_storage_api_converge_log(pcGlobalID, *pcReID, NULL, "SEC_storage_api_ofd_download()", body, acErrDesc, LOG_RESPONSE);
				break;
			default :
				break;
		}
	}

	iRet = (iErrCount == pstRecvStatus->iFdNum ? FAILURE : SUCCESS);

	// 释放内存
	free(pstRecvStatus);
	free(pucBuff);
	return iRet;
}

/**
 * 函数功能：下载PDF文件
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucBytes[in]：存放下载到的二进制数据
 *      @puiLen[in]：数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_pdf_download(void *phHandle, unsigned char *pucInvoiceID, 
				unsigned char **pucBytes, unsigned int *puiLen, char *pcGlobalID, char *pcParentID, char **pcReID) {

	// 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || strlen(pucInvoiceID) == 0 || pucBytes == NULL || puiLen == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.");
        return ERROR_INVALID_PARAM;
	}

	*pucBytes = NULL;

	return SEC_storage_api_download(phHandle, pucInvoiceID, pucBytes, puiLen, CMD_DOWNLOAD_PDF, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：下载图片文件
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucBytes[in]：存放下载到的二进制数据
 *      @puiLen[in]：数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_img_download(void *phHandle, unsigned char *pucInvoiceID, 
				unsigned char **pucBytes, unsigned int *puiLen, char *pcGlobalID, char *pcParentID, char **pcReID) {

	// 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || strlen(pucInvoiceID) == 0 || pucBytes == NULL || puiLen == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.");
        return ERROR_INVALID_PARAM;
	}

	*pucBytes = NULL;

	return SEC_storage_api_download(phHandle, pucInvoiceID, pucBytes, puiLen, CMD_DOWNLOAD_IMG, pcGlobalID, pcParentID, pcReID);
}

/**
 * 函数功能：下载OFD数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucBytes[in]：存放下载到的二进制数据
 *      @puiLen[in]：数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_ofd_download(void *phHandle, unsigned char *pucInvoiceID, 
				unsigned char **pucBytes, unsigned int *puiLen, char *pcGlobalID, char *pcParentID, char **pcReID) {

	// 检验参数有效性
	if(phHandle == NULL || pucInvoiceID == NULL || strlen(pucInvoiceID) == 0 || pucBytes == NULL || puiLen == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.");
        return ERROR_INVALID_PARAM;
	}

	*pucBytes = NULL;

	return SEC_storage_api_download(phHandle, pucInvoiceID, pucBytes, puiLen, CMD_DOWNLOAD_OFD, pcGlobalID, pcParentID, pcReID);
}
