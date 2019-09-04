#include <stdio.h>
#include <errno.h>
#include <time.h>

#if defined(_WIN32)
     #include <winsock2.h>
     #pragma comment(lib, "Ws2_32.lib")
#endif

#ifndef _DEBUG_H
#define _DEBUG_H

time_t g_t;				// 存放当前时间
char g_acTimeStr[26];	// 存放当前时间字符串

/*
 * 功能：打印调试信息
 * 参数：
 *		@pcFilePath[in]：源文件名称
 *		@iLineNum[in]：行号
 *		@pcFunc[in]：函数名
 * 返回值：无
 */
#ifdef DEBUG
    #define SEC_storage_api_log_print(pcFilePath, iLineNum, pcFunc) { \
        printf("filename is %s, line number is %d, function name is %s\n", pcFilePath, iLineNum, pcFunc); \
    }
#else
    #define SEC_storage_api_log_print(...)
#endif

/*
 * 功能：打印错误信息
 * 参数：
 *		@pcFilePath[in]：源文件名称
 *		@iLineNum[in]：行号
 *		@pcFunc[in]：函数名
 *		@pcErrDesc[in]：错误详细描述
 * 返回值：无
 */
#if defined(__linux__)
#define SEC_storage_api_err_print(pcFilePath, iLineNum, pcFunc, pcErrDesc) { \
		g_t = time(NULL); \
		strftime(g_acTimeStr, 26, "%Y-%m-%d %H:%M:%S", localtime(&g_t)); \
        printf("%s, filename is %s, line number is %d, function name is %s, ", g_acTimeStr, pcFilePath, iLineNum, pcFunc); \
		if(errno != 0) {\
			perror(pcErrDesc); \
		} else { \
			printf("%s\n", pcErrDesc); \
		} \
}
#elif defined(_WIN32)
#define SEC_storage_api_err_print(pcFilePath, iLineNum, pcFunc, pcErrDesc) { \
		g_t = time(NULL); \
		memset(g_acTimeStr, 0, 26); \
		strftime(g_acTimeStr, 26, "%Y-%m-%d %H:%M:%S", localtime(&g_t)); \
        printf("%s, filename is %s, line number is %d, function name is %s, ", g_acTimeStr, pcFilePath, iLineNum, pcFunc); \
		if(errno != 0) { \
			perror("pcErrDesc"); \
		} else if(GetLastError() != 0) { \
			printf("GetLastError() %d\n", GetLastError()); \
		} else if(WSAGetLastError() != 0) { \
			printf("WSAGetLastError() %d\n", WSAGetLastError()); \
		} else { \
			printf("%s\n", pcErrDesc); \
		} \
}
#endif

/*
 * 功能：打印数据的16进制形式
 * 参数：
 *		@pcBuff[in]：字节数组起始位置
 *		@iDataLen[in]：字节数组长度
 * 返回值：无
 */
#define SEC_storage_api_print_hex(pcBuff, iDataLen) { \
        int i = 0;  \
        for(; i < iDataLen; i++)  \
        {  \
            printf("%02x ", *(pcBuff + i));  \
            if((i + 1) % 10 == 0)  \
                printf("\n");  \
        }  \
        printf("\n"); \
}

#endif
