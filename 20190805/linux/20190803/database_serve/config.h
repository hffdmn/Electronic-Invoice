#ifndef _CONFIG_H
#define _CONFIG_H

struct SEC_StorageDbHandle {
	MYSQL *pConn;
};

/**
 * 函数功能：解析配置文件，获得连接信息，返回句柄
 * 参数：
 *      @pstHandle[out]：数据库连接句柄
 *      @pcsCfgPath[in]：配置文件路径
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
int SEC_storage_api_db_init(struct SEC_StorageDbHandle *pstHandle, const char* pcsCfgPath);

/**
 * 函数功能：释放数据库连接句柄所占用的内存 
 * 参数：
 *     @pstHandle[in]：连接句柄
 * 返回值：无
 */
void SEC_storage_api_db_free(struct SEC_StorageDbHandle *pstHandle);

#endif
