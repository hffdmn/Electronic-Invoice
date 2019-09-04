#ifndef _QUERY_H
#define _QUERY_H

// 根据不同的平台添加导出函数
#if defined(__linux__)
	#define EXPORT
#elif defined(_WIN32)
	#define EXPORT __declspec(dllexport)
#endif

/**
 * 函数功能：释放查询json数据占用的内存
 * 参数：
 *      @pucJsonData[in]：数组首地址；
 *      @piDataNum[in]：数组中元素个数
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_json_free(unsigned char **pucJsonData, int piDataNum);

/**
 * 函数功能：释放文件数据占用的内存
 * 参数：
 *      @pucBuff[in]：数组首地址；
 * 返回值：SUCCESS 表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_buff_free(unsigned char *pucBuff);

/**
 * 函数功能：根据发票ID进行查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_invoiceId_query(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：多id批量查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucQueryJson[in]：多条invoiceid组成的json字符串
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_invoiceId_batch_query(void *phHandle, unsigned char *pucQueryJson, 
		unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID);


/**
 * 函数功能：根据发票要素进行查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucQueryJson[in]：查询条件组成的json字符串
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_elem_query(void *phHandle, unsigned char *pucQueryJson, 
		unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：根据普通字段进行查询
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucQueryJson[in]：查询条件组成的json字符串
 *      @iOffset[in]：分页偏移地址
 *      @iNum[in]：分页结果数
 *      @pucJsonData[out]：指向一块内存区域，用于存放查询到的json数据
 *      @puiDataNum[out]：指针查询结果数
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_normal_query(void *phHandle, unsigned char *pucQueryJson, int iOffset, 
		int iNum, unsigned char ***pucJsonData, int *piDataNum, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：下载PDF文件
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucBytes[out]：存放下载到的二进制数据
 *      @puiLen[out]：数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_pdf_download(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char **pucBytes, unsigned int *puiLen, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：下载图片文件
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucBytes[out]：存放下载到的二进制数据
 *      @puiLen[out]：数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_img_download(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char **pucBytes, unsigned int *puiLen, char *pcGlobalID, char *pcParentID, char **pcReID);

/**
 * 函数功能：下载OFD数据
 * 参数：
 *      @phHandle[in]：连接句柄
 *      @pucInvoiceID[in]：发票ID
 *      @pucBytes[out]：存放下载到的二进制数据
 *      @puiLen[out]：数据长度
 *      @pcGlobalID[in]：全局ID，异常汇聚方案所需
 *      @pcParentID[in]：父ID，异常汇聚方案所需
 *		@pcReID[out]：响应ID，异常汇聚方案所需
 * 返回值：SUCCESS表示成功，FAILURE表示失败
 */
EXPORT 
int SEC_storage_api_ofd_download(void *phHandle, unsigned char *pucInvoiceID, 
		unsigned char **pucBytes, unsigned int *puiLen, char *pcGlobalID, char *pcParentID, char **pcReID);
#endif
