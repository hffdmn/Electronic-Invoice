#ifndef _SERVER_H
#define _SERVER_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#include "packet.h"

#define MAX_BACKLOG 1000

// 缓冲区大小，4MB
#define MAX_BUFF_LEN (4 * 1024 * 1024)

// 缓冲区中数据累计超过阈值之后就进行发送
#define SEND_THRESHOLD (MAX_BUFF_LEN - MAX_PKT_LEN)

/**
 * piDownEpollFd：下行epoll句柄
 * pDownThreads：数据下行线程
 * upThread：数据上行线程
 * iListenFd：监听用户连接的套接字
 * pMtxs：每个下行线程一个互斥锁
 * iDownThreadNum：下行线程数
 * maxEventNum：支持的最大连接数
 */
struct SEC_DbAgent {
	int *piDownEpollFd;
	pthread_t *pDownThreads;
	int iListenFd;
	int iUpSocket;
	pthread_mutex_t *pMtxs;
	int iDownThreadNum;
	int iMaxEventNum;
};

/**
 * 该数据结构用于创建Epoll线程时向线程传递参数，
 * i用于区分不同线程，pstAgent指向保存数据库连接信息的
 * 数据结构可以通过i来找到为该线程分配的的资源，
 * 例如pstGateway->fds[i]找到该线程的Epoll句柄等。
 * i：用于区分不同的线程
 * pstAgent：保存服务端信息
 */
struct SEC_EpollParam {
	int i;
	struct SEC_DbAgent *pstAgent;
};

/**
 * iFd：接受和发送数据的socket文件描述符
 * iLen：buffer中的数据长度
 * aucData：存放数据的数据缓冲区
 */
struct SEC_EpollData {
	int iFd;
	int iLen;
	unsigned char aucData[MAX_PKT_LEN];
};

#endif
