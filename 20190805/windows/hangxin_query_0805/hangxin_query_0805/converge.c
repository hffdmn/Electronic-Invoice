#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
	#include <sys/time.h>
#elif defined(_WIN32)
	#include <sys/timeb.h>
	#include <winsock.h>
#endif

#include "cJSON.h"
#include "converge.h"

// 汇聚服务器IP地址和端口号
//#define IP ("124.127.47.86")
//#define PORT (7070)

// 函数返回结果
#define SUCCESS 0

/*
 * 函数功能：返回当前毫秒时间，Linux下与Widnows下方式不一样
 * 参数：无
 * 返回值：时间的毫秒值
 */
static 
long long get_timestamp() {
#if defined(__linux__)
	// 该结构体由秒和微秒组成
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
#elif defined(_WIN32)
	// 该结构体由秒和毫秒组成
	struct timeb tb;
	ftime(&tb);
	return (tb.time * 1000 + tb.millitm);
#endif
}

/*
 * 函数功能：向日常日志汇聚系统发送请求接收日志
 * 函数参数：
 *		@pcGloablID[in]：全局ID
 *		@pcReId[in]：响应ID
 *		@pcParentID[in]：父ID
 *		@pcAction[in]：执行函数名
 *		@body[in]：接口的参数内容
 *		@pcErrContent[in]：错误内容
 *		@iLogType[in]：日志类型
 * 返回值：SUCCESS，日志存储成功
 *		   其他值，日志存储失败
 */
EXPORT 
int SEC_storage_api_converge_log(char *pcGlobalID, char *pcReID, char *pcParentReid, 
							char *pcAction, cJSON *body, char *pcErrContent, int iLogType) {
	
	char acTimeStamp[64];					// 将时间毫秒值转化为字符串
	cJSON *log;								// 异常日志结构的根结点
	cJSON *parameter;						// 异常日志parameter成员
	cJSON *clientMsg;						// 异常日志clientMsg成员
	cJSON *requestAction;					// 异常日志requestAction成员
	int iRet;								// 函数返回结果
	char *pcJsonStr;						// 异常日志json格式字符串
	//char *pcService = "Storage system";	//服务名称
	char *pcService = "海量存储系统";		//服务名称

	// 将毫秒值变为字符串
	memset(acTimeStamp, 0, 64);
	sprintf(acTimeStamp, "%lld", get_timestamp());

	// 添加日志结构的各项成员
	log = cJSON_CreateObject();
	cJSON_AddStringToObject(log, "serviceName", pcService);
	cJSON_AddStringToObject(log, "spanTimestamp", acTimeStamp);
	cJSON_AddStringToObject(log, "globalID", pcGlobalID);
	cJSON_AddStringToObject(log, "reID", pcReID);

	// 根据日志埋点类型添加父ID
	if(iLogType == LOG_REQUEST) {
		cJSON_AddStringToObject(log, "parentReid", pcParentReid);
	} else if(iLogType == LOG_RESPONSE) {
		//cJSON_AddStringToObject(log, "parentReid", "null");
	}
	parameter = cJSON_CreateObject();
	clientMsg = cJSON_CreateObject();
	requestAction = cJSON_CreateObject();
	cJSON_AddStringToObject(requestAction, "Action", pcAction);
    cJSON_AddItemToObject(clientMsg, "RequestAction", requestAction);
	cJSON_AddItemToObject(clientMsg, "Body", body);
	cJSON_AddItemToObject(parameter, "ClientMsg", clientMsg);
	cJSON_AddItemToObject(log, "parameter", parameter);

	// 根据日志埋点类型添加spanType
	if(iLogType == LOG_REQUEST) {
		cJSON_AddNumberToObject(log, "spanType", 1);
	} else if(iLogType == LOG_RESPONSE) {
		cJSON_AddNumberToObject(log, "spanType", 8);
	}
	cJSON_AddStringToObject(log, "errContent", pcErrContent);

	// 将日志cJSON结构转化为字符串
	iRet = SUCCESS;
	pcJsonStr = cJSON_PrintUnformatted(log);

	// 连接汇聚服务器
	//iRet = SEC_converge_client_init(IP, PORT);
	//if(iRet != SUCCESS) {
    //    printf("converge_client_init failure, %d.\n", iRet);
	//	return iRet;
	//}
	
	// 发送日志数据
	iRet = SEC_converge_client_send(pcJsonStr, strlen(pcJsonStr));
	if(iRet != SUCCESS) {
        printf("converge_client_send failure, %d.\n", iRet);
		return iRet;
	}

	// 关闭与汇聚服务器的连接
	//SEC_converge_client_close();

	// 释放内存
	cJSON_Delete(log);
	free(pcJsonStr);
	return SUCCESS;
}
