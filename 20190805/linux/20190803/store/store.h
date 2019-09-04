#ifndef _STORE_H
#define _STORE_H

// 设置导出函数
#if defined(__linux__)
	#define EXPORT
#elif defined(_WIN32)
	#define EXPORT __declspec(dllexport)
#endif

/**
 * 函数功能：存储json格式发票数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的发票数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_json(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：存储pdf格式发票数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：pdf文件二进制数据
 *      @uiFileLen[in]：pdf数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_pdf(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucFileBuff, unsigned int uiFileLen, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：存储备注图片数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：备注图片二进制数据
 *      @uiFileLen[in]：备注图片数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_img(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucFileBuff, unsigned int uiFileLen, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：存储ofd格式发票数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucFileBuff[in]：ofd文件二进制数据
 *      @uiFileLen[in]：ofd数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_store_ofd(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucFileBuff, unsigned int uiFileLen, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：更新发票状态信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_update_status(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：更新发票所有权信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_update_ownership(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：更新报销信息
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[in]：json格式的更新数据
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_update_reim(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char *pucJsonData, char *pcGlobalID, char *pcParentID, char **pcReID);

#endif
