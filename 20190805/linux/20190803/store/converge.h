#ifndef _CONVERGE_H
#define _CONVERGE_H

// 设置导出函数
#if defined(__linux__)
	#define EXPORT
#elif defined(_WIN32)
	#define EXPORT __declspec(dllexport)
#endif

#include "SEC_converge_client.h"

// 不同的日志埋点类型
#define LOG_REQUEST 1
#define LOG_RESPONSE 2

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
int SEC_storage_api_converge_log(char *pcGlobalID, char *pcReID, 
	char *pcParentReid, char *pcAction, cJSON *body, char *pcErrContent, int iLogType);

#endif
