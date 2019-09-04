#ifndef _NSP_MYSQL_H
#define _NSP_MYSQL_H

#include <mysql.h>

#include "config.h"
#include "packet.h"

/**
 * 函数功能：解析json数据，插入到数据库表中，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_store_json(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

/**
 * 函数功能：将二进制形式的文件数据插入到数据库表中，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_store_file(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

/**
 * 函数功能：更新发票的状态信息，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_update_status(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

/**
 * 函数功能：更新发票的所有权信息，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_update_ownership(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

 /**
  * 函数功能：更新发票的报销信息，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_update_reim(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

 /**
  * 函数功能：根据发票ID查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_id_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

/**
  * 函数功能：根据多发票ID批量查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_id_batch_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

 /**
  * 函数功能：根据发票关键要素查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_elem_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

 /**
  * 函数功能：根据发票普通字段查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_normal_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);

 /**
  * 函数功能：根据发票ID查询文件数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_file_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader);
#endif
