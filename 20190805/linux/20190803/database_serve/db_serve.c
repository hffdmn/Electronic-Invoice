#include "server.h"
#include "packet.h"
#include "utils.h"
#include "nsp_mysql.h"
#include "config.h"
#include "debug.h"

// 配置文件路径
#define FILEPATH "db.ini"

// db_serve的配置信息
#define PORT 12347
#define THREAD_NUM 1
#define EVENTS_NUM 1000000

// Epoll事件处理过程中的两个状态
#define S100 1
#define S200 2

// 返回结果
#define SUCCESS 0
#define FAILURE 1

// 连接句柄
struct SEC_StorageDbHandle *g_pstHandle;

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
		if(iLeft < uiLen + sizeof(struct SEC_Header)) { // 判断剩余的数据够不够一个完整的作业包
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
    struct SEC_DbAgent *pstAgent;    // 数据库信息 
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
	MYSQL_RES *pResult;						// MySQL查询结果
	int iProcLen;							// 已经处理的数据长度
	struct SEC_Header *pstHeader;			// 数据包头部

	// 为线程获取对应编号的epoll句柄，分配epoll事件数组
	pstEp = (struct SEC_EpollParam*)param;
	pstAgent = pstEp->pstAgent;
	iEpollFd = pstAgent->piDownEpollFd[pstEp->i];
	pstEvents = (struct epoll_event*)malloc(sizeof(struct epoll_event) * pstAgent->iMaxEventNum);
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
				iFds = epoll_wait(iEpollFd, pstEvents, pstAgent->iMaxEventNum, -1);
				if(iFds <= 0) {
					SEC_storage_api_err_print(__FILE__, __LINE__,__FUNCTION__, "epoll_wait()");
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
						pthread_mutex_lock(&pstAgent->pMtxs[pstEp->i]);
						epoll_ctl(iEpollFd, EPOLL_CTL_DEL, pstEd->iFd, NULL);
						pthread_mutex_unlock(&pstAgent->pMtxs[pstEp->i]);
						close(pstEd->iFd);
						free(pstEd);
						continue;
					}
					
					// 有新连接到来，添加新的socket到不同的epoll线程
					if(pstEd->iFd == pstAgent->iListenFd) {
						//printf("accpet events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
						printf("In epoll_thread, accept event\n");
						iSockFd = accept(pstEd->iFd, NULL, NULL);
						pstNewEd = (struct SEC_EpollData*)malloc(sizeof(struct SEC_EpollData));
						pstNewEd->iFd = iSockFd;
						pstNewEd->iLen = 0;
						stEv.data.ptr = pstNewEd;
						stEv.events = EPOLLIN | EPOLLRDHUP;

						pstAgent->iUpSocket = iSockFd;

						//通过sockfd进行分配，保证一个线程socket连接关闭后一个客户端立马能添上一个，每个线程负载均衡
						iRoll = iSockFd % pstAgent->iDownThreadNum;
						pthread_mutex_lock(&pstAgent->pMtxs[iRoll]);
						epoll_ctl(pstAgent->piDownEpollFd[iRoll], EPOLL_CTL_ADD, iSockFd, &stEv);
						pthread_mutex_unlock(&pstAgent->pMtxs[iRoll]);
						continue;
					}

					// 处理数据的业务逻辑部分
					if(pstEvents[i].events & EPOLLIN) {
						//printf("recv events[i].events = %x, ed->fd = %d\n", pstEvents[i].events, pstEd->iFd);
						//SEC_storage_api_log_print("In epoll_thread, read event\n");

						// 首先将上次未处理完的数拷贝到缓冲区
						if(pstEd->iLen > 0) {
							memcpy(pucWbuff + iWlen, pstEd->aucData, pstEd->iLen);
						}

						// 接收新的数据
						iLen = recv(pstEd->iFd, pucWbuff + iWlen + pstEd->iLen, MAX_BUFF_LEN - iWlen - pstEd->iLen, 0);

						if(iLen > 0) {
							//返回有效作业包的总长度
							iFullLen = SEC_storage_api_check_pkt(pucWbuff + iWlen, iLen + pstEd->iLen);
							iWlen += iFullLen;  // 更新缓冲区数据总长度
							iWleft = iLen + pstEd->iLen - iFullLen; // 剩余不完整包的数据长度
						} else { // recv函数出错
							SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "recv()");
							perror("recv()");
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
						continue;
					}
				}
				if(iWlen > 0) {
					iStatus = S200;
				}
				break;
			case S200 :
				// 解析用户请求的类型并进行相应的处理
				iProcLen = 0;
				while(iWlen > 0) {
					// 解析包头部
					pstHeader = (struct SEC_Header*)(pucWbuff + iProcLen);
					iProcLen += sizeof(struct SEC_Header);

					switch(pstHeader->ucCmd) {
						case CMD_STORE_PDF :	// 存储PDF数据
						case CMD_STORE_IMG :	// 存储IMG数据
						case CMD_STORE_OFD :	// 存储OFD数据
							SEC_storage_api_store_file(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_STORE_JSON :  // 存储JSON数据
							SEC_storage_api_store_json(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_STATUS_UPDATE :	// 更新状态信息
							SEC_storage_api_update_status(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_OWNERSHIP_UPDATE :	// 更新所有权信息
							SEC_storage_api_update_ownership(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_REIM_UPDATE :	// 更新报销信息
							SEC_storage_api_update_reim(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_ID_QUERY:  // 根据发票ID查询数据
							SEC_storage_api_mysql_id_query(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_IDBATCH_QUERY:  // 根据多发票ID批量查询数据
							SEC_storage_api_mysql_id_batch_query(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_ELEM_QUERY :	// 根据发票要素查询数据
							SEC_storage_api_mysql_elem_query(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_NORMAL_QUERY :	// 根据一般发票字段查询数据
							SEC_storage_api_mysql_normal_query(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						case CMD_DOWNLOAD_PDF :	// 下载PDF数据
						case CMD_DOWNLOAD_IMG :	// 下载IMG数据
						case CMD_DOWNLOAD_OFD :	// 下载OFD数据
							SEC_storage_api_mysql_file_query(g_pstHandle->pConn, pucWbuff + iProcLen, pstAgent->iUpSocket, pstHeader);
							break;
						default:
							break;
					}
					// 更新长度值 
					iWlen = iWlen - pstHeader->uiLen - sizeof(struct SEC_Header);
					iProcLen = iProcLen + pstHeader->uiLen;
				}
				iStatus = S100;
				break;
			default :
				break;
		}		
	}
	return;
}

/**
 * 函数功能：设置网关启动是需要的参数值，如线程数，每个epoll线程的最大事件数等。
 * 参数：
 *     @pstGateway[in]：全局配置信息
 *     @iThreadNum[in]：线程数
 *     @iMaxConn[in]：最大并发连接数
 * 返回值：
 *     SUCCESS表示操作成功，FAILURE表示操作失败
 */
static
int SEC_storage_api_agent_init(struct SEC_DbAgent *pstAgent, int iThreadNum, int iMaxConn) {

	int i;	// 循环标记
	
	// 检验参数的有效性
	if(pstAgent == NULL || iThreadNum <= 0 || iMaxConn <= 0) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.");
        return FAILURE;
	}

	// Epoll线程数
	pstAgent->iDownThreadNum = iThreadNum;
	
	// 每个Epoll线程负责的平均连接数
	pstAgent->iMaxEventNum = iMaxConn / iThreadNum + 1;

	// Epoll文件描述符数组,下行线程
	pstAgent->piDownEpollFd = (int*)malloc(sizeof(int) * iThreadNum);
	if(pstAgent->piDownEpollFd == NULL)	{
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	} 
	
	// 对Epoll进行同步操作的互斥量数组，每个Epoll拥有一个
	pstAgent->pMtxs = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * iThreadNum);
	if(pstAgent->pMtxs == NULL)	{
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
	
	// Epoll线程数组，下行线程
	pstAgent->pDownThreads = (pthread_t *)malloc(sizeof(pthread_t) * iThreadNum);
	if(pstAgent->pDownThreads == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}

	// 每个线程分得一个Epoll描述符和一个互斥量，下行线程
	for(i = 0; i < iThreadNum; i++) {
		pstAgent->piDownEpollFd[i] = epoll_create(pstAgent->iMaxEventNum);
		pthread_mutex_init(&pstAgent->pMtxs[i], NULL);
	}
	return SUCCESS;
}

/**
 * 函数功能：绑定监听端口，创建Epoll线程
 * 参数：
 *     @pstAgent[in]：存有pstAgent的配置信息
 *     @usPort：监听端口
 * 返回值：
 *     SUCCESS表示成功，FAILURE表示失败
 */
int SEC_storage_api_agent_start(struct SEC_DbAgent *pstAgent, unsigned short usPort) {

     int i;                              // 循环标记
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
	epoll_ctl(pstAgent->piDownEpollFd[0], EPOLL_CTL_ADD, iListenFd, &stEv);
	pstAgent->iListenFd = iListenFd;
	
	g_pstHandle = (struct SEC_StorageDbHandle*)malloc(sizeof(struct SEC_StorageDbHandle));
	if(SEC_storage_api_db_init(g_pstHandle, FILEPATH) == FAILURE) {
		return FAILURE;
	}

	// 创建epoll线程，并传递参数，下行线程
	pstEp = (struct SEC_EpollParam*)malloc(sizeof(struct SEC_EpollParam) * 1);
	if(pstEp == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "malloc()");
		return FAILURE;
	}
	for(i = 0; i < pstAgent->iDownThreadNum; i++) {
		pstEp[i].i = i;
		pstEp[i].pstAgent = pstAgent;
		if(pthread_create(&pstAgent->pDownThreads[i], NULL, SEC_storage_api_down_thread, (void *)&pstEp[i]) != 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_create()");
			return FAILURE;
		}
	}

	// 等待epoll线程结束
	for (i = 0; i < pstAgent->iDownThreadNum; ++i) {
		if(pthread_join(pstAgent->pDownThreads[i], NULL) != 0) {
			SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "pthread_join()");
			return FAILURE;
		}
	}
	return SUCCESS;
}

int main() {

	struct SEC_DbAgent stAgent;
	memset(&stAgent, 0, sizeof(struct SEC_DbAgent));

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

	// 用于保障多线程的环境，要不然之后的mysql_init函数会出错
	mysql_library_init(0, NULL, NULL);
	
    // 传入Epoll线程数和总的连接数 
	if(SEC_storage_api_agent_init(&stAgent, THREAD_NUM, EVENTS_NUM) == FAILURE)	{
		printf("Program init error, exit.\n");
		return FAILURE;
	} else {
		printf("Program init successfully.\n");
	}
	
	// 启动程序
	if(SEC_storage_api_agent_start(&stAgent, PORT) == FAILURE) {
		printf("Program start error, exit.\n");
		return FAILURE;
	} else {
		printf("Program start successfully.\n");
	}

	return 0;
}
