#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#include "parse.h"
#include "err.h"
#include "utils.h"
#include "debug.h"
#include "iniparser.h"

// 文本行大小 
#define MAX_LINE 1024

// 函数返回值
#define SUCCESS 0
#define FAILURE 1

/**
 * 函数功能：解析配置文件，连接一级网关，返回句柄
 * 参数：
 *      @phHandle[out]：连接句柄指针
 *      @pcsCfgPath[out]：配置文件路径
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
int SEC_storage_api_init(void **phHandle, const char* pcsCfgPath) {

	dictionary *pConfig;		// 配置信息键值对
	char acKey[MAX_LINE];		// 文本行缓冲
	const char *pcGroupName;	// 设备组名称
    const char *pcIp;			// IP地址
    unsigned short usPort;		// 连接端口
	// 记录设备组以及设备信息
    struct SEC_StorageAPIHandle *pstHandle;
	int i, j;					//循环标记
    int iNodeNum;				// 设备组中的设备数
	int iSock;					// socket描述符

    // 确认参数有效
    if(phHandle == NULL || pcsCfgPath == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "invalid parameter.");
        return FAILURE;
    }

    // 分配存储空间
    pstHandle = (struct SEC_StorageAPIHandle*)malloc(sizeof(struct SEC_StorageAPIHandle));
    if(pstHandle == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
        return ERROR_MALLOC;
    }
    memset(pstHandle, 0, sizeof(struct SEC_StorageAPIHandle));

	// 解析配置文件
	pConfig = iniparser_load(pcsCfgPath);
	if(pConfig == NULL) {
		// 打印出错信息，退出程序
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "iniparser_load()");
		return FAILURE;
	}

	// 解析设备组数
	pstHandle->iGroupNum = iniparser_getnsec(pConfig);
    if(pstHandle->iGroupNum <= 0 || pstHandle->iGroupNum > 100) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "iniparser_getnsec()");
		return FAILURE;
    }

    // 分配连接句柄空间
    pstHandle->pstGroup = (struct SEC_StorageGroup*)malloc(pstHandle->iGroupNum * sizeof(struct SEC_StorageGroup));
    if(pstHandle->pstGroup == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return ERROR_MALLOC;
	}

	// 逐个解析每个一级网关的连接参数信息
	for(i = 0; i < pstHandle->iGroupNum; i++) {

		pcGroupName = iniparser_getsecname(pConfig, i);
		if(pcGroupName == NULL) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Group name is NULL.");
			return FAILURE;
		}

		memset(acKey, 0, MAX_LINE);
		sprintf(acKey, "%s:%s", pcGroupName, "nodeNum");

		iNodeNum = iniparser_getint(pConfig, acKey, 0);
		if(iNodeNum <= 0 || iNodeNum > 1000) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid node num.");
			return FAILURE;
		}
        pstHandle->pstGroup[i].iDeviceNum = iNodeNum;
        pstHandle->iTotalDevice += iNodeNum;

        // 分配连接句柄空间
        pstHandle->pstGroup[i].piSockFd = (int*)malloc(iNodeNum * sizeof(int));
        if(pstHandle->pstGroup[i].piSockFd == NULL) {
            // 打印出错信息，退出程序
            SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
            return ERROR_MALLOC;
        }

        // 分配连接句柄空间
        pstHandle->pstGroup[i].pstAddr = (struct sockaddr_in*)malloc(iNodeNum * sizeof(struct sockaddr_in));
        if(pstHandle->pstGroup[i].pstAddr == NULL) {
            SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
            return ERROR_MALLOC;
        }

        // 分配连接句柄空间
        pstHandle->pstGroup[i].pucStatus = (unsigned char*)malloc(iNodeNum * sizeof(unsigned char));
        if(pstHandle->pstGroup[i].pucStatus == NULL) {
            // 打印出错信息，退出程序
            SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
            return ERROR_MALLOC;
        }

        for(j = 0; j < iNodeNum; j++) {

            memset(acKey, 0, MAX_LINE);
			sprintf(acKey, "%s:%s%d:%s", pcGroupName, "node", j, "ip");
			pcIp = iniparser_getstring(pConfig, acKey, NULL);
			if(pcIp == NULL) {
			    SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "IP is NULL.");
		        return FAILURE;
	        }

            memset(acKey, 0, MAX_LINE);
			sprintf(acKey, "%s:%s%d:%s", pcGroupName, "node", j, "port");
			usPort = (unsigned short)iniparser_getint(pConfig, acKey, 0);
			if(usPort <= 0 || usPort > 65535) {
			    SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid port.");
		        return FAILURE;
	        }

            memset(acKey, 0, MAX_LINE);
			sprintf(acKey, "%s:%s%d:%s", pcGroupName, "node", j, "status");
			pstHandle->pstGroup[i].pucStatus[j] = (unsigned char)iniparser_getint(pConfig, acKey, 0);
			if(pstHandle->pstGroup[i].pucStatus[j] != 0 &&  pstHandle->pstGroup[i].pucStatus[j] != 1) {
			    SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid status.");
		        return FAILURE;
	        }

            pstHandle->pstGroup[i].pstAddr[j].sin_family = AF_INET;
            pstHandle->pstGroup[i].pstAddr[j].sin_port = htons(usPort);
            pstHandle->pstGroup[i].pstAddr[j].sin_addr.s_addr = inet_addr(pcIp);

            // 进行socket连接
            iSock = socket(AF_INET, SOCK_STREAM, 0);
            if(connect(iSock, (struct sockaddr*)(&(pstHandle->pstGroup[i].pstAddr[j])), sizeof(struct sockaddr)) < 0) {
                SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "connect()");
				printf("Can not connect to fir_gate %s\n", pcIp);
                return FAILURE;
            }

            // 将句柄值赋给全局信息
            pstHandle->pstGroup[i].piSockFd[j] = iSock;
        }
	}

	*phHandle = pstHandle;
	iniparser_freedict(pConfig);
	return SUCCESS;
}

/**
 * 函数功能：释放连接句柄
 * 参数：
 *      @phHandle[in]：句柄指针；
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
int SEC_storage_api_free(void *phHandle) {

    struct SEC_StorageAPIHandle *pstHandle;
    int i, j;

    // 检验参数有效性
    if(phHandle == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.");
        return FAILURE;
    }

    pstHandle = phHandle;

    // 循环关闭数组中的每一个socket
    for(i = 0; i < pstHandle->iGroupNum; i++) {
		for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) {
			close(pstHandle->pstGroup[i].piSockFd[j]);
		}
        free(pstHandle->pstGroup[i].piSockFd);
		free(pstHandle->pstGroup[i].pstAddr);
		free(pstHandle->pstGroup[i].pucStatus);
    }

    // 释放所有地址信息
    free(pstHandle->pstGroup);

    // 释放全局信息
	free(phHandle);
}
