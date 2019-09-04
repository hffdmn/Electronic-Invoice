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

#define MAX_BUFF_LEN (4 * 1024 * 1024)

#define SUCCESS 0
#define FAILURE 1

#define ID_LEN 32
char g_acRanID[ID_LEN + 1];

/**
 * 函数功能：接收指定长度的数据，最终接收到的数据大于等于这个长度
 * 参数：
 *     @iFd[in]：socket描述符
 *     @pucData[out]：数据缓冲区
 *     @piTotal[in]：接收数据的起始偏移位置
 *     @iDataLen[in]：指定的数据长度
 * 返回值：成功返回SUCCESS，失败返回其他值
 */
int SEC_storage_api_recvEx(int iFd, unsigned char *pucData, int *piTotal, int iDataLen) {

	int iLen;		// 记录每次接收的数据长度

	iLen = 0;
	while(*piTotal < iDataLen) { // 判断数据是否全部发送完成
		iLen = recv(iFd, pucData + *piTotal, MAX_BUFF_LEN - *piTotal, 0);  // 发送数据
		if(iLen > 0) {
			*piTotal += iLen;  // 更新offset值
		} else {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "recv() error");
			return ERROR_SOCKET;
		}
	}
    return SUCCESS;
}

/**
 * 函数功能：发送指定长度的数据
 * 参数：
 *     @iFd[in]：socket描述符
 *     @pucData[in]：数据缓冲区
 *     @iDataLen[in]：数据长度
 * 返回值：成功返回SUCCESS，失败返回FAILURE
 */
int SEC_storage_api_sendEx(int iFd, unsigned char *pucData, int iDataLen) {

	int iOffset;  // 记录发送了多少数据
	int iLen;  // 记录每次发送的数据长度
	
	iOffset = 0;
	while(iOffset < iDataLen) { // 判断数据是否全部发送完成
		iLen = send(iFd, pucData + iOffset, iDataLen - iOffset, 0);  // 发送数据
		if(iLen > 0) {
			iOffset += iLen;  // 更新offset值
		} else {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "send()");
			return FAILURE;
		}
	}
    return SUCCESS;
}

/*
 * 函数功能：计算字符串的hash值，java自带的字符串哈希函数
 * 输入：
 *     @pcStr[in]：用于哈希计算的字符串
 * 输出：哈希计算结果
 */
unsigned int SEC_storage_api_jHash(const char *pcStr) {
	// 计算hash
    unsigned int uiHash = 0;
    int i = 0;
    int iLen = strlen(pcStr);

    for(i = 0; i < iLen; pcStr++, i++) {
        uiHash = 31 * uiHash + (*pcStr);
    }
	return uiHash;
}

/**
 * 函数功能：计算哈希值，SDBMHash
 * 输入：
 *     @pcStr[in]：进行哈希计算的字符串
 * 输出：哈希计算结果
 */
unsigned int SEC_storage_api_sdbmHash(const char *pcStr) {
	// 计算hash
    unsigned int uiHash = 0;
    int i = 0;
    int iLen = strlen(pcStr);

    for(i = 0; i < iLen; pcStr++, i++) {
        uiHash = (*pcStr) + (uiHash << 6) + (uiHash << 16) - uiHash;
    }
	return uiHash;
}

/**
 * 函数功能：计算哈希值，ELFHash
 * 输入：
 *     @pcStr[in]：进行哈希计算的字符串
 * 输出：哈希计算结果
 */
unsigned int SEC_storage_api_elfHash(const char *pcStr) {
	// 计算hash 
	unsigned int uiHash = 0;
	unsigned int x = 0;
    int i = 0;
    int iLen = strlen(pcStr);

    for(i = 0; i < iLen; pcStr++, i++) {
		uiHash = (uiHash << 4) + (*pcStr);
		if((x = uiHash & 0xF0000000L) != 0) {
			uiHash ^= (x >> 24);
		}
		uiHash &= ~x;
    }
	return uiHash;
}

/*
 * 函数功能：生成32字节随机ID
 * 参数：无
 * 返回值：32字节随机ID字符串
 */
char *SEC_storage_api_randomID() {

	int i;
	int iFlag;
	// 设置随机种子
	srand(SEC_storage_api_timestamp());
	for(i = 0; i < ID_LEN; i++) {
		iFlag = rand() % 3;
		switch(iFlag) {
			case 0:
				// 从a-z中随机取值
				g_acRanID[i] = rand() % 26 + 'a';
				break;
			case 1:
				// 从A-Z中随机取值
				g_acRanID[i] = rand() % 26 + 'A';
				break;
			case 2:
				// 从0-9中随机取值
				g_acRanID[i] = rand() % 10 + '0';
				break;
			default :
				break;
		}	
	}
	return g_acRanID;
}

/*
 * 函数功能：返回当前毫秒时间，Linux下与Widnows下方式不一样
 * 参数：无
 * 返回值：时间的毫秒值
 */
long long SEC_storage_api_timestamp() {
#if defined(__linux__)
	// 该结构体有妙和微妙组成
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
#elif defined(_WIN32)
	// 该结构体由妙和毫秒组成
	struct timeb tb;
	ftime(&tb);
	return (tb.time * 1000 + tb.millitm);
#endif
}
