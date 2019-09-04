#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql.h>

#include "config.h"
#include "utils.h"
#include "debug.h"
#include "iniparser.h"

// 函数返回结果
#define SUCCESS 0
#define FAILURE 1

/**
 *  函数功能：处理MySQL错误 
 *  参数：
 *      @pConn[in]：MySQL连接句柄
 *  返回值：无
 */
static 
void finish_with_error(MYSQL *pConn) {
	// 打印详细出错信息，关闭数据库连接
	fprintf(stderr, "%s\n", mysql_error(pConn));
	mysql_close(pConn);
}         

/**
 * 函数功能：解析配置文件，获得连接信息，返回句柄
 * 参数：
 *      @pstHandle[out]：数据库连接句柄
 *      @pcsCfgPath[in]：配置文件路径
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
int SEC_storage_api_db_init(struct SEC_StorageDbHandle *pstHandle, const char* pcsCfgPath) {

	dictionary *pConfig;		// 配置信息键值对
	const char *pcIp;			// 数据库IP地址
	const char *pcUsername;		// 用户名
	const char *pcPassword;		// 密码
	const char *pcDbname;		// 数据库名
	unsigned short usPort;		// 连阶段端口
	char iRetry;				// MySQL重连选项

	// 确认参数有效
    if(pstHandle == NULL || pcsCfgPath == NULL) {
        SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid parameter.");
        return FAILURE;
    }

	// 家在配置文件
	pConfig = iniparser_load(pcsCfgPath);
	if(pConfig == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "iniparse_load()");
		return FAILURE;
	}
	//iniparser_dump(pConfig, stderr);

	// 数据库服务器IP地址
	pcIp = iniparser_getstring(pConfig, ":address", NULL);
	if(pcIp == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid IP.");
		return FAILURE;
	}

	// 数据库用户名
	pcUsername = iniparser_getstring(pConfig, ":username", NULL);
	if(pcUsername == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid username.");
		return FAILURE;
	}

	// 数据库密码
	pcPassword = iniparser_getstring(pConfig, ":password", NULL);
	if(pcPassword == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid password.");
		return FAILURE;
	}

	// 数据库名
	pcDbname = iniparser_getstring(pConfig, ":dbname", NULL);
	if(pcDbname == NULL) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid dbname.");
		return FAILURE;
	}

	// 连接端口
	usPort = iniparser_getint(pConfig, ":port", 0);
	if(usPort < 0 || usPort > 65535) {
		SEC_storage_api_err_print(__FILE__, __LINE__, __FUNCTION__, "Invalid port.");
		return FAILURE;
	}

	// 初始化MySQL句柄
	pstHandle->pConn = mysql_init(NULL);
	if(pstHandle->pConn == NULL) {
		finish_with_error(pstHandle->pConn);
		return FAILURE;
	}
	
	// 设置Mysql重连
	iRetry = 1;
	mysql_options(pstHandle->pConn, MYSQL_OPT_RECONNECT, (char*)&iRetry);

	// 连接数据库
	if (mysql_real_connect(pstHandle->pConn, pcIp, pcUsername, pcPassword, pcDbname, usPort, 
		NULL, CLIENT_FOUND_ROWS) == NULL) {
		finish_with_error(pstHandle->pConn);
		return FAILURE;
	}

	// 设置mysql连接使用utf8进行传输
	mysql_set_character_set(pstHandle->pConn, "utf8");

	// 关闭文件，返回连接参数
	return SUCCESS;
}

/**
 * 函数功能：释放数据库连接句柄所占用的内存 
 * 参数：
 *     @pstHandle[in]：连接句柄
 * 返回值：无
 */
void SEC_storage_api_db_free(struct SEC_StorageDbHandle *pstHandle) {
	// 关闭数据库
	mysql_close(pstHandle->pConn);

	// 释放申请的内存
	free(pstHandle);
}
