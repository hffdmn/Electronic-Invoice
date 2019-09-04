#include "server.h"
#include "packet.h"
#include "utils.h"
#include "parse.h"
#include "debug.h"

// 配置文件路径
#define FILEPATH "gateway.ini"

// 网关的配置信息
#define PORT 12346 
#define THREAD_NUM 1
#define EVENTS_NUM 1000000

// Epoll事件处理过程中的两个状态
#define S100 1
#define S200 2

// 返回结果
#define SUCCESS 0
#define FAILURE 1

// 连接句柄
struct SEC_StorageAPIHandle *g_pstHandle;

/**
 * 函数功能：通过socket描述符获取对端的IP地址
 * 参数：
 *     @iSock[in]：socket描述符
 * 返回值：IP地址
 */ 
static
char *SEC_storage_api_get_ip_by_fd(struct SEC_StorageAPIHandle *pstHandle, int iSock) {

    int i, j;       // 循环标记

	for(i = 0; i < pstHandle->iGroupNum; i++) {
		for(j = 0; j < pstHandle->pstGroup[i].iDeviceNum; j++) {
			if(pstHandle->pstGroup[i].piSockFd[j] == iSock) {
				return inet_ntoa(pstHandle->pstGroup[i].pstAddr[j].sin_addr);
			}
		}
    }   
   // 正常情况下程序不后走到这里
   return NULL;
}

/* 函数功能：从缓冲区中解析TLV，获取发票ID
 * 参数：
 *      @pucBuff[in]：数据缓冲区
 *      @pucInvoiceID[out]：发票ID
 * 返回值：无
 */
static
void SEC_storage_api_get_invoice_id(unsigned char *pucBuff, unsigned char *pucInvoiceID) {
	
	struct SEC_TLV *pstTlv;  // 各字段TLV

	// 解析TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pucBuff;  // 解析包头
		pucBuff += sizeof(struct SEC_TLV);  // 更新buff下标的位置
		if(pstTlv->ucType == FIELD_EOF) {
			return;
		} else if(pstTlv->ucType != FIELD_ID) { // TLV解析完毕，跳出循环
			pucBuff += pstTlv->uiLen;
			continue;
		} else if(pstTlv->ucType == FIELD_ID){
			memcpy(pucInvoiceID, pucBuff, pstTlv->uiLen);
			return;
		} 
	}
}

/**
 * 函数功能：发送缓冲区中的数据
 * 参数：
 *     @pucBuff[in]：存放数据的缓冲区
 *     @iWlen[in]：缓冲区中的数据长度
 * 返回值：成功发送的数据长度
 */
static
int SEC_storage_api_send_data(unsigned char *pucBuff, int iWlen) {

    unsigned char ucReqType;				// 请求类型，存储请求或查询请求
    unsigned int uiDataLen;					// 数据包长度
    struct SEC_Header *pstHeader;			// 存储请求或查询请求数据包头
    int iSendLen;							// 记录发送数据的长度
    unsigned int uiHash;					// 哈希计算结果
    unsigned int uiIndex;					// 取模计算结果
    unsigned char aucInvoiceID[40];			// 发票ID
	int i, j;								// 循环标记
	struct SEC_RecvStatus *pstRecvStatus;	// 记录请求的返回状态
	
	memset(aucInvoiceID, 0, 40);
	iSendLen = 0;

	// 循环发送缓冲区中的数据
	while(iSendLen < iWlen) {
		pstHeader = (struct SEC_Header*)(pucBuff + iSendLen);
		uiDataLen = pstHeader->uiLen;
			
		// 先将数据恢复为结构体
		SEC_storage_api_get_invoice_id(pucBuff + sizeof(struct SEC_Header), aucInvoiceID);

		if(strlen(aucInvoiceID) == 0) { // 如果不存在发票ID

			pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
			pstRecvStatus->iFdNum = 0;
			pstRecvStatus->ucUpdateRes = 1;

			for(i = 0; i < g_pstHandle->iGroupNum; i++) {
				pstRecvStatus->iFdNum += g_pstHandle->pstGroup[i].iDeviceNum;
			}
			pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
			pstHeader->pointer3 = (void*)pstRecvStatus;

			// 广播数据
			for(i = 0; i < g_pstHandle->iGroupNum; i++) {
				for(j = 0; j < g_pstHandle->pstGroup[i].iDeviceNum; j++) {
					SEC_storage_api_sendEx(g_pstHandle->pstGroup[i].piSockFd[j], pucBuff + iSendLen, uiDataLen + sizeof(struct SEC_Header));
				}
			}
		} else { // 如果存在发票ID
			switch(pstHeader->ucCmd) {

				// 存储类请求
				case CMD_STORE_JSON :
				case CMD_STORE_PDF :
				case CMD_STORE_IMG :
				case CMD_STORE_OFD :

					// 进行哈希和取模计算
					uiHash = SEC_storage_api_elfHash(aucInvoiceID);
					uiIndex = uiHash % g_pstHandle->iGroupNum;

					// 为了防止存储到同一个节点，采用轮询的方式
					for(; ; ) {
						g_pstHandle->pstGroup[uiIndex].uiCounter++;
						if(g_pstHandle->pstGroup[uiIndex].pucStatus[g_pstHandle->pstGroup[uiIndex].uiCounter % g_pstHandle->pstGroup[uiIndex].iDeviceNum] == 1) {
							// 发送存储请求，返FAILURE说明socket出错
							if(SEC_storage_api_sendEx(g_pstHandle->pstGroup[uiIndex].piSockFd[g_pstHandle->pstGroup[uiIndex].uiCounter % g_pstHandle->pstGroup[uiIndex].iDeviceNum], pucBuff + iSendLen, uiDataLen + sizeof(struct SEC_Header)) == FAILURE) {
								printf("In epoll thread, send to db_serve %s error, ingore it.\n", inet_ntoa(g_pstHandle->pstGroup[uiIndex].pstAddr[g_pstHandle->pstGroup[uiIndex].uiCounter % g_pstHandle->pstGroup[uiIndex].iDeviceNum].sin_addr));
							}
							break;
						}
					}
				break;

				//查询、下载和更新请求
				case CMD_ID_QUERY :
				case CMD_ELEM_QUERY :
				case CMD_NORMAL_QUERY :
				case CMD_STATUS_UPDATE :
				case CMD_OWNERSHIP_UPDATE :
				case CMD_REIM_UPDATE :
				case CMD_DOWNLOAD_PDF :
				case CMD_DOWNLOAD_IMG :
				case CMD_DOWNLOAD_OFD :

					// 进行哈希计算和取模计算
					uiHash = SEC_storage_api_elfHash(aucInvoiceID);
					uiIndex = uiHash % g_pstHandle->iGroupNum;

					// 因为要发送给多个节点，所以需要记录该请求的状态信息
					struct SEC_RecvStatus *pstRecvStatus = (struct SEC_RecvStatus*)malloc(sizeof(struct SEC_RecvStatus));
					pstRecvStatus->iFdNum = g_pstHandle->pstGroup[uiIndex].iDeviceNum;
					pstRecvStatus->iLeft = pstRecvStatus->iFdNum;
					pstRecvStatus->ucUpdateRes = 1;

					pstHeader->pointer3 = (void*)pstRecvStatus;

					// 将请求发送到设备组中的所有节点
					for(i = 0; i < g_pstHandle->pstGroup[uiIndex].iDeviceNum; i++) {
						if(SEC_storage_api_sendEx(g_pstHandle->pstGroup[uiIndex].piSockFd[i], pucBuff + iSendLen, uiDataLen + sizeof(struct SEC_Header)) == FAILURE) {
							printf("In epoll thread, send to db_serve %s error, ingore it.\n", inet_ntoa(g_pstHandle->pstGroup[uiIndex].pstAddr[i].sin_addr));
							printf("Lost connection with db_serve, please check.\n");
						}
					}

				break;
				default :
				break;
			}
		}
	iSendLen = iSendLen + uiDataLen + sizeof(struct SEC_Header);
	}
	return iSendLen;					
}

/**
 * 函数功能：返回完整作业包的长度
 * 参数：
 *     @pucBuff[in]：存放数据的缓冲区
 *     @iDataLen[in]：缓冲区中的数据长度
 * 返回值：完整作业包长度
 */
static
int SEC_storage_api_check_pkt(unsigned char *pucBuff, int iDataLen) {

	int iFullLen;					// 记录总的有效包长度
	int iLeft;						// 剩余数据长度
	struct SEC_Header *pstHeader;	// 数据包头
	unsigned int uiLen;				// TLV数据长度

	// 初始化变量
	iFullLen = 0;
	iLeft = iDataLen;

	while(iLeft >= sizeof(struct SEC_Header)) { // 如果剩余的数据够一个包头

		// 解析包头
		pstHeader = (struct SEC_Header*)(pucBuff + iFullLen); 
		
		uiLen = pstHeader->uiLen;  // 取出该包中的数据长度
		if(iLeft < uiLen + sizeof(struct SEC_Header)) {  // 判断剩余的数据够不够一个完整的作业包
			break;
		}

		// 更新有效包长度和剩余数据长度
		iFullLen = iFullLen + sizeof(struct SEC_Header) + uiLen;
		iLeft = iLeft - sizeof(struct SEC_Header) - uiLen;
	}
	return iFullLen;
}

/**
 * 函数功能：为events分配空间，该数据结构线程独立，无需服务器统一管理，作为线程局部变量即可。
 *        1）如果是listenfd有输入则为有新连接，为其分配NspEpollData，保存sockfd并均匀添加到
 *           不同线程的监听事件中。但可能会出现一个线程存活率很高。另一线程客户端大部分都断开连接导致不>均衡。
 *           可采用sockfd % threadNum的方式保证每个线程处理的连接数量相近。
 *        2）如果是连接有输入则读入数据并处理。先将该连接上次未处理完的数据加入buff，
 *           再将从该连接读入的数据加入buff，然后进行处理。将本次未处理完的数据再保存到每个连接的data中>，
 *           下次事件发生时处理。
 *        3）将断开的连接释放掉。
 * 参数：
 *     @param[in]：SEC_EpollParam结构体指针，用于传递网关的相关信息
 * 返回值：无 
 */
static 
void *SEC_storage_api_down_thread(void *param) {

    struct SEC_EpollParam *pstEp;           // 自定义的传给每个epoll线程的参数
    struct SEC_SecondGateway *pstGateway;   // 网关配置信息 
    int iEpollFd;                           // epoll句柄
    int iSockFd;                            // 新接收的socket连接描述符
    struct epoll_event *pstEvents;          // epoll事件
    cpu_set_t cs;                           // cpu集合
    struct epoll_event stEv;                // epoll事件
    struct SEC_EpollData *pstEd, *pstNewEd; //自定义的epoll数据
    int iRoll;                              // 用于选择将socket文件描述符添加到哪一个epoll句柄中
    unsigned char *pucWbuff;                // 数据缓冲区
    int iLen, iFullLen;                     // 临时记录数据长度
    int iWlen;                              // 缓冲区中完整包的数据长度
    int iWleft;                             // 缓冲区中不完整包的数据长度 
    int iStatus;                            // Epoll事件处理的状态 
    int i;                                  // 循环标记
    int iFds;                               // 产生事件的描述符数

	// 为线程获取对应编号的epoll句柄，分配epoll事件数组
	pstEp = (struct SEC_EpollParam*)param;
	pstGateway = pstEp->pstGateway;
	iEpollFd = pstGateway->piDownEpollFd[pstEp->i];
	pstEvents = (struct epoll_event*)malloc(sizeof(struct epoll_event) * pstGateway->iMaxEventNum);
	pucWbuff = (unsigned char*)malloc(sizeof(unsigned char) * MAX_BUFF_LEN);  //分配缓冲区

	// 线程绑定cpu核
	CPU_ZERO(&cs);
	CPU_SET(pstEp->i, &cs);
	sched_setaffinity(0, sizeof(cpu_set_t), &cs);

	// 循环功能：
	// 1）监听socket连接，将新产生的文件描述符通过取模的方式添加到对应的epoll句柄
	// 2）关闭挂断或者出错的socket
	// 3）处理正常数据到来时的业务逻辑
	iStatus = S100;
	iWlen = 0;
	for(;;) {
		switch(iStatus) {
			case S100 :
				// 等待epoll事件发生		
				iFds = epoll_wait(iEpollFd, pstEvents, pstGateway->iMaxEventNum, -1);
				if(iFds <= 0) {
					SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "epoll_wait()");
					continue;
				}
				// 遍历有事件发生的socket集合
				for(i = 0; i < iFds; i++) {
					pstEd = (struct SEC_EpollData*)pstEvents[i].data.ptr;
					
					// 对端socket关闭或者出错，断开连接
					if(pstEvents[i].events & EPOLLRDHUP
						|| pstEvents[i].events & EPOLLHUP
						|| pstEvents[i].events & EPOLLERR) {
						//printf("in epoll_thread close events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
						printf("In epoll_thread, close event\n");
						// 操作时需要加锁保护
						pthread_mutex_lock(&pstGateway->pMtxs[pstEp->i]);
						epoll_ctl(iEpollFd, EPOLL_CTL_DEL, pstEd->iFd, NULL);
						pthread_mutex_unlock(&pstGateway->pMtxs[pstEp->i]);
						close(pstEd->iFd);
						free(pstEd);
						continue;
					}
					
					// 有新连接到来，添加新的socket到不同的epoll线程
					if(pstEd->iFd == pstGateway->iListenFd) {
						//printf("in epoll_thread accpet events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
						printf("In epoll_thread, accept event\n");
						iSockFd = accept(pstEd->iFd, NULL, NULL);
						pstNewEd = (struct SEC_EpollData*)malloc(sizeof(struct SEC_EpollData));
						pstNewEd->iFd = iSockFd;
						pstNewEd->iLen = 0;
						stEv.data.ptr = pstNewEd;
						stEv.events = EPOLLIN | EPOLLRDHUP;

						pstGateway->iUpSocket = iSockFd;

						//通过sockfd进行分配，保证一个线程socket连接关闭后一个客户端立马能添上一个，每个线程负载均衡
						// 操作时需要加锁保护
						iRoll = iSockFd % pstGateway->iDownThreadNum;
						pthread_mutex_lock(&pstGateway->pMtxs[iRoll]);
						epoll_ctl(pstGateway->piDownEpollFd[iRoll], EPOLL_CTL_ADD, iSockFd, &stEv);
						pthread_mutex_unlock(&pstGateway->pMtxs[iRoll]);
						continue;
					}

					// 处理数据的业务逻辑部分
					if(pstEvents[i].events & EPOLLIN) {
						//printf("in epoll_thread read events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
						//SEC_storage_api_log_print("In epoll_thread, read event\n");
						// 首先将上次未处理完的数拷贝到缓冲区
						if(pstEd->iLen > 0) {
							memcpy(pucWbuff + iWlen, pstEd->aucData, pstEd->iLen);
						}

						// 接收数据
						iLen = recv(pstEd->iFd, pucWbuff + iWlen + pstEd->iLen, MAX_BUFF_LEN - iWlen - pstEd->iLen, 0);						
						if(iLen > 0) {
							//返回有效作业包的总长度
							iFullLen = SEC_storage_api_check_pkt(pucWbuff + iWlen, iLen + pstEd->iLen);
							iWlen += iFullLen;  // 更新缓冲区数据总长度
							iWleft = iLen + pstEd->iLen - iFullLen; // 剩余不完整包的数据长度
						} else {  // recv函数出错
							SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "recv()");
							iWleft = pstEd->iLen;
						}

						// 将不完整的数据拷贝回socket绑定的缓冲区内，更新数据长度
						pstEd->iLen = iWleft;
						if(iWleft > 0) {
							memcpy(pstEd->aucData, pucWbuff + iWlen, iWleft);
						}

						// 缓冲区中的数据大于阈值，直接跳出循环去处理数据
						if(iWlen >= SEND_THRESHOLD) {
							break;
						}
						continue;
					}
				}
				if(iWlen > 0) {
					iStatus = S200;
				}
				break;
			case S200 :
				// 发送数据
				SEC_storage_api_send_data(pucWbuff, iWlen);
				iStatus = S100;
				iWlen = 0;
				break;
			default :
				break;
		}
	}
	return;
}

/**
 * 函数功能：发送缓冲区中的数据
 * 参数：
 *     @iSockFd[in]：与一级网关通信的socket描述符
 *     @pucBuff[in]：存放数据的缓冲区
 *     @iDataLen[in]：缓冲区中的数据长度
 * 返回值：成功返回SUCCESS；失败返回FAILURE
 */
static
int SEC_storage_api_send_up(int iSockFd, unsigned char *pucBuff, int iDataLen) {

	struct SEC_Header *pstHeader;	// 数据包头部
	int iLen;						// 记录当前已经发送的数据长度

	// 初始化变量
	iLen = 0;

	while(iLen < iDataLen) { // 判断是否全部数据都发完了
		pstHeader = (struct SEC_Header*)(pucBuff + iLen);  // 解析TLV头部
		if(pstHeader->ucIsLast == 1) {
			// 获取该请求的状态信息，判断是否所有节点发送数据完毕
			struct SEC_RecvStatus *pstRecvStatus = (struct SEC_RecvStatus*)(pstHeader->pointer3);
			if(pstRecvStatus->iLeft > 1) {	// 仍然有节点没有发送完数据
				switch(pstHeader->ucCmd) {
					case CMD_UPDATE_RES:	// 更新操作需要记录是否有成功的数据包
						iLen += sizeof(struct SEC_Header) + pstHeader->uiLen;
						 if(pstHeader->ucCmdRes == SUCCESS && pstHeader->uiLen == 0) {
							 pstRecvStatus->ucUpdateRes = 0;
						 }
						break;
					default :	// 其他操作直接跳过该数据包
						iLen += sizeof(struct SEC_Header) + pstHeader->uiLen;
						break;
				}
				(pstRecvStatus->iLeft)--;
				continue;
			} else {	// 等于1，表明最后一个节点发送数据完毕
				// 这里根据iUpdateRes修改最后一次的结果
				if(pstHeader->ucCmd == CMD_UPDATE_RES && pstRecvStatus->ucUpdateRes == 0) {
					if(pstHeader->uiLen == 0) {
						pstHeader->ucCmdRes = 0;
					}
				}
				free(pstRecvStatus);
			}
		}
	
		// 发送数据
		if(SEC_storage_api_sendEx(iSockFd, pucBuff + iLen, pstHeader->uiLen + sizeof(struct SEC_Header)) == FAILURE) {
			 printf("In up thread, send to fir_gate %s error, ingore it.\n", SEC_storage_api_get_ip_by_fd(g_pstHandle, iSockFd));
			//printf("Lost connection with fir_gate, please check.\n");
		}

		iLen += pstHeader->uiLen + sizeof(struct SEC_Header);  // 更新iLen
	}
	return SUCCESS;
}

/**
 * 函数功能：为events分配空间，该数据结构线程独立，无需服务器统一管理，作为线程局部变量即可。
 *        1）将程序初始化时已经和db_serve建立好的连接添加到Epoll当中，等待事件发生，期间不会再有新连接到来
 *        2）如果是连接有输入则读入数据并处理。先将该连接上次未处理完的数据加入buff，
 *           再将从该连接读入的数据加入buff，然后进行处理。将本次未处理完的数据再保存到每个连接的data中，
 *           下次事件发生时处理。
 *        3）将断开的连接释放掉。
 * 参数：
 *     @param[in]：SEC_EpollParam结构体指针，用于传递网关的相关信息
 * 返回值：无
 */
static 
void *SEC_storage_api_up_thread(void *param) {

    struct SEC_EpollParam *pstEp;           // 自定义的传给每个epoll线程的参数
	struct SEC_SecondGateway *pstGateway;    // 网关状态信息 
	int iEpollFd;                           // epoll句柄
    struct epoll_event *pstEvents;          // epoll事件
    cpu_set_t cs;                           // cpu集合
    struct epoll_event stEv;                // epoll事件
    struct SEC_EpollData *pstEd, *pstNewEd; //自定义的epoll数据
    unsigned char *pucWbuff;                // 数据缓冲区
    int iLen, iFullLen;                     // 临时记录数据长度
    int iWlen;                              // 缓冲区中完整包的数据长度
    int iWleft;                             // 缓冲区中不完整包的数据长度 
    int i, j;                               // 循环标记
    int iFds;                               // 产生事件的描述符数

	// 为线程获取对应编号的epoll句柄，分配epoll事件数组
	pstEp = (struct SEC_EpollParam*)param;
	pstGateway = pstEp->pstGateway;
	iEpollFd = pstGateway->iUpEpollFd;
	pstEvents = (struct epoll_event*)malloc(sizeof(struct epoll_event) * pstGateway->iMaxEventNum);
	pucWbuff = (unsigned char*)malloc(sizeof(unsigned char) * MAX_BUFF_LEN);  //分配缓冲区

	// 将socket句柄数组添加到epoll中
	for(i = 0; i < g_pstHandle->iGroupNum; i++) {
		for(j = 0; j < g_pstHandle->pstGroup[i].iDeviceNum; j++){
			pstNewEd = (struct SEC_EpollData*)malloc(sizeof(struct SEC_EpollData));
			pstNewEd->iFd = g_pstHandle->pstGroup[i].piSockFd[j];
			pstNewEd->iLen = 0;
			stEv.data.ptr = pstNewEd;
			stEv.events = EPOLLIN | EPOLLRDHUP;

			epoll_ctl(iEpollFd, EPOLL_CTL_ADD, g_pstHandle->pstGroup[i].piSockFd[j], &stEv);
		}
	}

	// 线程绑定cpu核
	CPU_ZERO(&cs);
	CPU_SET(pstEp->i, &cs);
	sched_setaffinity(0, sizeof(cpu_set_t), &cs);

	// 循环功能：
	// 1）关闭挂断或者出错的socket
	// 2）处理正常数据到来时的业务逻辑
	iWlen = 0;
	for(;;) {
		// 等待epoll事件发生		
		iFds = epoll_wait(iEpollFd, pstEvents, pstGateway->iMaxEventNum, -1);
		if(iFds <= 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "epoll_wait()");
			continue;
		}

		// 遍历有事件发生的socket集合
		for(i = 0; i < iFds; i++) {
			pstEd = (struct SEC_EpollData*)pstEvents[i].data.ptr;
			
			// 对端socket关闭或者出错，断开连接
			if(pstEvents[i].events & EPOLLRDHUP
				|| pstEvents[i].events & EPOLLHUP
				|| pstEvents[i].events & EPOLLERR) {
				//printf("in up_thread close events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
				printf("In up_thread, close socket with db_serve %s\n", SEC_storage_api_get_ip_by_fd(g_pstHandle, pstEd->iFd));
				// 操作时需要加锁保护
				pthread_mutex_lock(&pstGateway->pMtxs[pstEp->i]);
				epoll_ctl(iEpollFd, EPOLL_CTL_DEL, pstEd->iFd, NULL);
				pthread_mutex_unlock(&pstGateway->pMtxs[pstEp->i]);
				close(pstEd->iFd);
				free(pstEd);
				continue;
			}
			// 处理数据的业务逻辑部分
			if(pstEvents[i].events & EPOLLIN) {
				//printf("in up_thread read events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
				//SEC_storage_api_log_print("In up thread read from db_serve %s\n", SEC_storage_api_get_ip_by_fd(g_pstHandle, pstEd->iFd));

				// 首先将上次未处理完的数拷贝到缓冲区
				if(pstEd->iLen > 0)	{
					memcpy(pucWbuff + iWlen, pstEd->aucData, pstEd->iLen);
				}

				// 接收数据
				iLen = recv(pstEd->iFd, pucWbuff + iWlen + pstEd->iLen, MAX_BUFF_LEN - iWlen - pstEd->iLen, 0);

				if(iLen > 0) {
					//返回有效作业包的总长度
					iFullLen = SEC_storage_api_check_pkt(pucWbuff + iWlen, iLen + pstEd->iLen);
					iWlen += iFullLen;  // 更新缓冲区数据总长度
					iWleft = iLen + pstEd->iLen - iFullLen; // 剩余不完整包的数据长度
				} else {  // recv函数出错
					SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "recv()");
					iWleft = pstEd->iLen;
				}

				// 将不完整的数据拷贝回socket绑定的缓冲区内，更新数据长度
				pstEd->iLen = iWleft;
				if(iWleft > 0) {
					memcpy(pstEd->aucData, pucWbuff + iWlen, iWleft);
				}

				// 缓冲区中的数据大于阈值，直接跳出循环去处理数据
				if(iWlen >= SEND_THRESHOLD)	{
					break;
				}
			}
		}
		// 向一级网关发送所有的数据
		SEC_storage_api_send_up(pstGateway->iUpSocket, pucWbuff, iWlen);
		iWlen = 0;
	}
	return;
}

/**
 * 函数功能：设置网关启动是需要的参数值，如线程数，每个epoll线程的最大事件数等。
 * 参数：
 *     @pstGateway[in]：网关配置信息
 *     @iThreadNum[in]：线程数
 *     @iMaxConn[in]：最大并发连接数
 * 返回值：
 *     SUCCESS表示操作成功，FAILURE表示操作失败
 */
static
int SEC_storage_api_second_gateway_init(struct SEC_SecondGateway *pstGateway, int iThreadNum, int iMaxConn) {
	
	int i;	// 循环标记

	// 检验参数的有效性
	if(pstGateway == NULL || iThreadNum <= 0 || iMaxConn <= 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.");
        return FAILURE;
	}

	// Epoll线程数
	pstGateway->iDownThreadNum = iThreadNum;
	
	// 每个Epoll线程负责的平均连接数
	pstGateway->iMaxEventNum = iMaxConn / iThreadNum + 1;

	// Epoll文件描述符数组,下行线程
	pstGateway->piDownEpollFd = (int*)malloc(sizeof(int) * iThreadNum);
	if(pstGateway->piDownEpollFd == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
	
	// 对Epoll进行同步操作的互斥量数组，每个Epoll拥有一个
	pstGateway->pMtxs = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * iThreadNum);
	if(pstGateway->pMtxs == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
	
	// Epoll线程数组，下行线程
	pstGateway->pDownThreads = (pthread_t*)malloc(sizeof(pthread_t) * iThreadNum);
	if(pstGateway->pDownThreads == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}

	// 每个线程分得一个Epoll描述符和一个互斥量，下行线程
	for(i = 0; i < iThreadNum; i++) {
		pstGateway->piDownEpollFd[i] = epoll_create(pstGateway->iMaxEventNum);
		if(pstGateway->piDownEpollFd[i] < 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "epoll_create()");
			return FAILURE;
		}
		if(pthread_mutex_init(&pstGateway->pMtxs[i], NULL) < 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_mutex_init()");
			return FAILURE;;
		}
	}
	// Epoll句柄，上行线程
	pstGateway->iUpEpollFd = epoll_create(pstGateway->iMaxEventNum);
	if(pstGateway->iUpEpollFd < 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "epoll_create()");
		return FAILURE;
	}
	return SUCCESS;
}

/**
 * 函数功能：绑定监听端口，创建Epoll线程
 * 参数：
 *     @pstGateway[in]：存有网关的配置信息
 *     @usPort：监听端口
 * 返回值：
 *     SUCCESS表示成功，FAILURE表示失败
 */
static
int SEC_storage_api_second_gateway_start(struct SEC_SecondGateway *pstGateway, unsigned short usPort) {

	 int i;								 // 循环标记
	 int iListenFd;                      // 用于监听socket连接的文件描述符
     int iReuse;                         // 地址复用标记
     struct sockaddr_in stServerAddr;    // 网络地址
     struct epoll_event stEv;            // Epoll事件结构体
     struct SEC_EpollData *pstEd;        // 自定义的Epoll数据
     struct SEC_EpollParam *pstEp;       // 传递给每个Epoll线程的参数

	// 创建文件描述符
	iListenFd = socket(AF_INET, SOCK_STREAM, 0);
	if(iListenFd < 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "socket()");
		return FAILURE;
	}

	// 设置地址复用
	iReuse = 1;
	if(setsockopt(iListenFd, SOL_SOCKET, SO_REUSEADDR, &iReuse, sizeof(int)) < 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "setsockopt()");
		return FAILURE;
	}

	// 绑定监听端口
	stServerAddr.sin_family = AF_INET;
	stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	stServerAddr.sin_port = htons(usPort);
	bind(iListenFd, (struct sockaddr*)&stServerAddr, sizeof(struct sockaddr));

	listen(iListenFd, MAX_BACKLOG);

	// 将用于监听网络连接的listenfd加入Epoll监听的事件当中
	pstEd = (struct SEC_EpollData*)malloc(sizeof(struct SEC_EpollData));
	pstEd->iFd = iListenFd;
	pstEd->iLen = 0;
	stEv.data.ptr = pstEd;
	stEv.events = EPOLLIN | EPOLLRDHUP;
	epoll_ctl(pstGateway->piDownEpollFd[0], EPOLL_CTL_ADD, iListenFd, &stEv);
	pstGateway->iListenFd = iListenFd;

	// 根据配置文件解析下层设备的ip，端口参数
	if(SEC_storage_api_init((void**)&g_pstHandle, FILEPATH) == FAILURE) {
		return FAILURE;
	}

	// 创建Epoll线程，并传递参数，下行线程
	pstEp = (struct SEC_EpollParam*)malloc(sizeof(struct SEC_EpollParam) * pstGateway->iDownThreadNum);
	if(pstEp == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
	for(i = 0; i < pstGateway->iDownThreadNum; i++)	{
		pstEp[i].i = i;
		pstEp[i].pstGateway = pstGateway;
		if(pthread_create(&pstGateway->pDownThreads[i], NULL, SEC_storage_api_down_thread, (void *)&pstEp[i]) != 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_create()");
			return FAILURE;
		}
	}

	// 创建Epoll线程，并传递参数，上行线程
	if(pthread_create(&pstGateway->upThread, NULL, SEC_storage_api_up_thread, (void*)&pstEp[0]) != 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_create()");
		return FAILURE;
	}

	// 等待Epoll线程结束
	for (i = 0; i < pstGateway->iDownThreadNum; ++i) {
		if(pthread_join(pstGateway->pDownThreads[i], NULL) != 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_join()");
			return FAILURE;
		}
	}
	if(pthread_join(pstGateway->upThread, NULL) != 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_join()");
		return FAILURE;
	}
	return SUCCESS;
}

int main() {

	struct SEC_SecondGateway stGateway;
	memset(&stGateway, 0, sizeof(struct SEC_SecondGateway));

	setbuf(stdout, NULL);

    //设置屏蔽信号
	signal(SIGCLD, SIG_IGN);
	// signal(SIGINT, SIG_IGN);
	signal(SIGSEGV, SIG_IGN);
	signal(SIGURG, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGABRT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

    //将当前进程设置为守护进程
	//daemon(1, 1);
	
	// 传入Epoll线程数和总的连接数 
	if(SEC_storage_api_second_gateway_init(&stGateway, THREAD_NUM, EVENTS_NUM) == FAILURE) {	
		printf("Program init error, exit.\n");
		return FAILURE;
	} else {
		printf("Program init successfully.\n");
	}
	
	// 启动网关
	if(SEC_storage_api_second_gateway_start(&stGateway, PORT) == FAILURE) {
		printf("Program start error, exit.\n");
		return FAILURE;
	} else {
		printf("Program start successfully.\n");
	}
	return 0;
}
