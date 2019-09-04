#ifndef _GATEWAY_H
#define _GATEWAY_H

/* 设备组以及组内设备信息，
 * 每个节点下可能有多个设备组，
 * 每个设备组中有多个结点
 */
struct SEC_StorageGroup {
	unsigned int uiCounter;
    int iDeviceNum;                 // 组中的节点数
    int *piSockFd;                  // 节点的socket描述符
    struct sockaddr_in *pstAddr;    // 节点的socket地址
    unsigned char *pucStatus;       // 节点的可用状态，1为可读写，0为可读
};

struct SEC_StorageAPIHandle {
    int iGroupNum;      // 设备组数
    int iTotalDevice;   // 所有组中的总设备数
    struct SEC_StorageGroup *pstGroup;
};

// 请求可能会发送给多个节点，此时需要记录该请求的状态
struct SEC_RecvStatus {
	int iFdNum;                 // 记录当前请求一共发送给了多少个节点
    int iLeft;                  // 记录还有多少节点没有返回数据完毕
    unsigned char ucUpdateRes;  // 记录更新操作的结果
};

/**
 * 函数功能：解析配置文件，连接一级网关，返回句柄
 * 参数：
 *      @phHandle[out]：连接句柄指针
 *      @pcsCfgPath[out]：配置文件路径
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
int SEC_storage_api_init(void **phHandle, const char* pcsCfgPath);

/**
 * 函数功能：释放连接句柄
 * 参数：
 *      @phHandle[in]：句柄指针；
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
int SEC_storage_api_free(void *phHandle);

#endif
