#include <stdio.h>
#include <stdlib.h>
#include <mysql.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nsp_mysql.h"
#include "utils.h"
#include "server.h"
#include "packet.h"
#include "debug.h"
#include "cJSON.h"

// 插入语句和查询语句的最大长度
#define MAX_INSERT_LINE (1024 * 1024)
#define MAX_QUERY_LINE 1024

// 操作结果
#define SUCCESS 0
#define	FAILURE 1

// normal_query submitterNum过滤
#define C_SUBMITTERNUM "submitterNum"
#define C_INVOWNNUM	 "invOwnNum" 

/**
 * 函数功能： 打印详细出错信息，关闭数据库连接，退出程序
 * 参数：
 *     @pConn[in]：MySQL连接句柄
 * 返回值：无
 */
static 
void finish_with_error(MYSQL *pConn) {
	fprintf(stderr, "%s\n", mysql_error(pConn));
	//mysql_close(conn);
	//exit(1);        
}


/**
 *	函数功能：检测json串中是否包含submitterNum且不包含invOwnNum
 *  如果是，则保存其值到submitterNum
 *  如果否，则将submitterNum设为NULL
 * 参数：
 *      @json[in]：查询条件组成的json对象
 * 		@submitterNum[out]: 报销人编号
 * 
 *
 * 例子：
 * "{\"submitterNum\":\"1\", \"invOwnNum\":\"1\"}";  // submitterNum != NULL
 * "{\"submitterNum\":\"1\", \"invOwnNum\":\"0\"}";  // submitterNum != NULL
 * "{\"time\":\"1\", \"invOwnNum\":\"1\"}"; 		 // submitterNum != NULL
 * "{\"submitterNum\":\"1\", \"time\":\"1\"}"; 		 // submitterNum == NULL
 */
static
void preCheck(cJSON *json, char **submitterNum) {
	char *invOwnNum = NULL;
	
	*submitterNum = cJSON_GetStringValue(cJSON_GetObjectItem(json, "submitterNum"));
	invOwnNum = cJSON_GetStringValue(cJSON_GetObjectItem(json, "invOwnNum"));
	
	if(*submitterNum && invOwnNum) {
		*submitterNum = NULL;
	}
}

/**
 * 函数功能：解析json数据，插入到数据库表中，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_store_json(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;		// 数据包头
	struct SEC_TLV *pstTlv;			// TLV结构
	char *pcInsertSql;				// 用于存放完整的插入语句
	char *pcJson;					// 用于存放请求中的json串
	char *pcKeys;					// 用于存放插入语句中的列名
	char *pcValues;					// 用于存放插入语句中的列值
	char acInvoiceID[40];			// 用于存放发票ID
	char acTemp[200];				// 用于存放从json中解析的键名和键值
	char acBuff[MAX_PKT_LEN];		// 用于构造返回结果包
	cJSON *root;					// json对象的根结点
	cJSON *elem;					// 一个最简单的cJSON对象 
	cJSON *itemArray;				// 发票中的item数组
	cJSON *item;					// item数组中的每一项
	cJSON *relatedArray;			// 发票中的related数组
	cJSON *related;					// related数组中的每一项
	int iItemArraySize;				// item数组大小
	int iRelatedArraySize;			// related数组大小
	int i;							// 循环标记
	unsigned int uiOffset;			// pcInsertSql中的偏移位置
	unsigned int uiKeysOffset;		// pcKeys中的偏移位置
	unsigned int uiValuesOffset;	// pcValues中的偏移位置
	int iMysqlErr;					// MySQL错误标志，1表示在处理过程中出现了错误
	const char *pcErrDesc;			// 错误原因的详细描述
	
	// 初始化变量
	pcInsertSql = NULL;
	pcJson = NULL;
	pcKeys = NULL;
	pcValues = NULL;
	root = NULL;
	uiOffset = 0;
	uiKeysOffset = 0;
	uiValuesOffset = 0;
	iMysqlErr = SUCCESS;
	memset(acInvoiceID, 0, 40);
	memset(acBuff, 0, MAX_PKT_LEN);
	// 填充包头字段
	stHeader.ucVersion = CUR_VERSION;
	stHeader.ucCmd = CMD_STORE_RES;  // 设置操作类型
	stHeader.iFd = pstHeader->iFd;  // 设置socket标识
	stHeader.uiLen = 1;
	stHeader.ucIsLast = 0;

	// 分配内存并且清零
	pcInsertSql = (unsigned char*)malloc(MAX_INSERT_LINE);
	memset(pcInsertSql, 0, MAX_INSERT_LINE);
	pcJson = (unsigned char*)malloc(MAX_INSERT_LINE);
	memset(pcJson, 0, MAX_INSERT_LINE);
	pcKeys = (unsigned char*)malloc(MAX_INSERT_LINE);
	memset(pcKeys, 0, MAX_INSERT_LINE);
	pcValues = (unsigned char*)malloc(MAX_INSERT_LINE);
	memset(pcValues, 0, MAX_INSERT_LINE);

	// 解析数据包中的TLV
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
		if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID :
				memcpy(acInvoiceID, pcBuff, pstTlv->uiLen);
				break;
			case FIELD_JSON :
				memcpy(pcJson, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
		}
		pcBuff += pstTlv->uiLen;
	}

	// 将json字符串转成cJSON结构
	root = cJSON_Parse(pcJson);

	// 初始化多个状态值 
	if(cJSON_GetObjectItem(root, "accStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "accStatus", elem);
	}
	//if(cJSON_GetObjectItem(root, "revStatus") == NULL) {
	//	elem = cJSON_CreateString("0");
	//	cJSON_AddItemToObject(root, "revStatus", elem);
	//}
	if(cJSON_GetObjectItem(root, "arcStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "arcStatus", elem);
	}
	//if(cJSON_GetObjectItem(root, "legalStatus") == NULL) {
	//	elem = cJSON_CreateString("0");
	//	cJSON_AddItemToObject(root, "legalStatus", elem);
	//}
	//if(cJSON_GetObjectItem(root, "issStatus") == NULL) {
	//	elem = cJSON_CreateString("0");
	//	cJSON_AddItemToObject(root, "issStatus", elem);
	//}
	//if(cJSON_GetObjectItem(root, "valStatus") == NULL) {
	//	elem = cJSON_CreateString("0");
	//	cJSON_AddItemToObject(root, "valStatus", elem);
	//}
	if(cJSON_GetObjectItem(root, "reiStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "reiStatus", elem);
	}
	if(cJSON_GetObjectItem(root, "payStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "payStatus", elem);
	}
	if(cJSON_GetObjectItem(root, "taxStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "taxStatus", elem);
	}
	if(cJSON_GetObjectItem(root, "tax-refundStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "tax-refundStatus", elem);
	}
	//if(cJSON_GetObjectItem(root, "voiStatus") == NULL) {
	//	elem = cJSON_CreateString("0");
	//	cJSON_AddItemToObject(root, "voiStatus", elem);
	//}
	if(cJSON_GetObjectItem(root, "colStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "colStatus", elem);
	}
	if(cJSON_GetObjectItem(root, "delStatus") == NULL) {
		elem = cJSON_CreateString("0");
		cJSON_AddItemToObject(root, "delStatus", elem);
	}

	// 解析json，拼接sql语句
	memcpy(pcKeys + uiKeysOffset, "(invoiceId,", strlen("(invoiceId,"));
	uiKeysOffset += strlen("(invoiceId,");
	memcpy(pcValues + uiValuesOffset, "values(", strlen("values("));
	uiValuesOffset += strlen("values(");
	memcpy(pcValues + uiValuesOffset, acInvoiceID, strlen(acInvoiceID));
	uiValuesOffset += strlen(acInvoiceID);
	memcpy(pcValues + uiValuesOffset, ",", strlen(","));
	uiValuesOffset += strlen(",");
    for(elem = root->child; elem != NULL; elem = elem->next) {
        memset(acTemp, 0, 200);
		if(strcmp(elem->string, "issuItemInformation") == 0 || strcmp(elem->string, "invoiceRelatedInformation") == 0 ||
			strcmp(elem->string, "layoutFile") == 0 || strcmp(elem->string, "remarkImg") == 0) {
			continue;
		} else if(strcmp(elem->string, "totalTax-includedAm") == 0 || strcmp(elem->string, "buyerE-mail") == 0 || 
			strcmp(elem->string, "2-dimensionalBarCode") == 0 || strcmp(elem->string, "tax-refundStatus") == 0 || 
			strcmp(elem->string, "invOwnE-mail") == 0) {
			// 中间有横线的列名需要用反引号括起来
			// 列值用单引号括起来 
			sprintf(acTemp, "`%s`,", elem->string);
			memcpy(pcKeys + uiKeysOffset, acTemp, strlen(acTemp));
			uiKeysOffset += strlen(acTemp);
		} else if(strcmp(elem->string, "invoiceId") == 0) {
			continue;	
		} else {
			sprintf(acTemp, "%s,", elem->string);
			memcpy(pcKeys + uiKeysOffset, acTemp, strlen(acTemp));
			uiKeysOffset += strlen(acTemp);
		}
		
		memset(acTemp, 0, 200);
		if(elem->type == cJSON_String) {
			sprintf(acTemp, "'%s',", elem->valuestring);
		} else if(elem->type == cJSON_Number) {
			sprintf(acTemp, "'%f',", elem->valuedouble);
		} else {
			// wrong type, handle error
		}
		memcpy(pcValues + uiValuesOffset, acTemp, strlen(acTemp));
        uiValuesOffset += strlen(acTemp);
    }
	// 拼接完毕，处理结尾部分
	memcpy(pcKeys + uiKeysOffset - 1, ") ", strlen(") "));
	uiKeysOffset += 1;
	pcValues[uiValuesOffset - 1] = ')';
	memcpy(pcInsertSql + uiOffset, "insert into basicInfo ", strlen("insert into basicInfo "));
	uiOffset += strlen("insert into basicInfo ");
	memcpy(pcInsertSql + uiOffset, pcKeys, uiKeysOffset);
	uiOffset += uiKeysOffset;
	memcpy(pcInsertSql + uiOffset, pcValues, strlen(pcValues));
	uiOffset += uiValuesOffset;
	printf("-------> basicInfo insert sql is %s\n", pcInsertSql);

	mysql_ping(pConn);
	// 关闭自动提交，开启事务操作
	mysql_query(pConn, "set autocommit = 0");
	mysql_query(pConn, "STRAT TRANSACTION");
	if(iMysqlErr == SUCCESS && mysql_query(pConn, pcInsertSql)) {
		fprintf(stderr, "%s\n", mysql_error(pConn));
		iMysqlErr = FAILURE;
	}

	// 获取item数组和related数组
	itemArray = cJSON_GetObjectItem(root, "issuItemInformation");
	relatedArray = cJSON_GetObjectItem(root, "invoiceRelatedInformation");

	// 逐项解析item数组，并向表中插入数据
	iItemArraySize = cJSON_GetArraySize(itemArray);
	for(i = 0; i < iItemArraySize; i++) {
		// 获取数组中的一项
		item = cJSON_GetArrayItem(itemArray, i);
	
		// 初始化变量
	 	memset(pcInsertSql, 0, MAX_INSERT_LINE);
		memset(pcKeys, 0, MAX_INSERT_LINE);
		memset(pcValues, 0, MAX_INSERT_LINE);
		uiOffset = 0;
		uiValuesOffset = 0;
		uiKeysOffset = 0;

		// 开始拼接sql语句
		memcpy(pcKeys + uiKeysOffset, "(invoiceId,", strlen("(invoiceId,"));
		uiKeysOffset += strlen("(invoiceId,");
		memcpy(pcValues + uiValuesOffset, "values(", strlen("values("));
		uiValuesOffset += strlen("values(");
		memcpy(pcValues + uiValuesOffset, acInvoiceID, strlen(acInvoiceID));
		uiValuesOffset += strlen(acInvoiceID);
		memcpy(pcValues + uiValuesOffset, ",", strlen(","));
		uiValuesOffset += strlen(",");
    	for(elem = item->child; elem != NULL; elem = elem->next) {
			// 获取键名
    	    memset(acTemp, 0, 200);
			sprintf(acTemp, "%s,", elem->string);
			memcpy(pcKeys + uiKeysOffset, acTemp, strlen(acTemp));
			uiKeysOffset += strlen(acTemp);
			
			// 获取值
			memset(acTemp, 0, 200);
			if(elem->type == cJSON_String) {
				sprintf(acTemp, "'%s',", elem->valuestring);
			} else if (elem->type == cJSON_Number) {
				sprintf(acTemp, "'%f',", elem->valuedouble);
			} else {
				// wrong type, handle error
			}
			memcpy(pcValues + uiValuesOffset, acTemp, strlen(acTemp));
    	    uiValuesOffset += strlen(acTemp);
    	}
		memcpy(pcKeys + uiKeysOffset - 1, ") ", strlen(") "));
		uiKeysOffset += 1;
		pcValues[uiValuesOffset - 1] = ')';
		// 将两部分合为完整的SQL语句
		memcpy(pcInsertSql + uiOffset, "insert into itemInfo ", strlen("insert into itemInfo "));
		uiOffset += strlen("insert into itemInfo ");
		memcpy(pcInsertSql + uiOffset, pcKeys, uiKeysOffset);
		uiOffset += uiKeysOffset;
		memcpy(pcInsertSql + uiOffset, pcValues, strlen(pcValues));
		uiOffset += uiValuesOffset;

		// 执行SQL语句
		printf("itemInfo insert sql is %s\n", pcInsertSql);
		if(iMysqlErr == SUCCESS && mysql_query(pConn, pcInsertSql)) {
			fprintf(stderr, "%s\n", mysql_error(pConn));
			iMysqlErr = FAILURE;
		}
	 }

	// 逐项解析related数组，并向表中插入数据
	iRelatedArraySize = cJSON_GetArraySize(relatedArray);
	for(i = 0; i < iRelatedArraySize; i++) {
		// 获取数组中的一项
	 	related = cJSON_GetArrayItem(relatedArray, i);

		// 初始化变量
	 	memset(pcInsertSql, 0, MAX_INSERT_LINE);
		memset(pcKeys, 0, MAX_INSERT_LINE);
		memset(pcValues, 0, MAX_INSERT_LINE);
		uiOffset = 0;
		uiValuesOffset = 0;
		uiKeysOffset = 0;

		// 开启拼接sql语句
		memcpy(pcKeys + uiKeysOffset, "(invoiceId,", strlen("(invoiceId,"));
		uiKeysOffset += strlen("(invoiceId,");
		memcpy(pcValues + uiValuesOffset, "values(", strlen("values("));
		uiValuesOffset += strlen("values(");
		memcpy(pcValues + uiValuesOffset, acInvoiceID, strlen(acInvoiceID));
		uiValuesOffset += strlen(acInvoiceID);
		memcpy(pcValues + uiValuesOffset, ",", strlen(","));
		uiValuesOffset += strlen(",");
    	for(elem = related->child; elem != NULL; elem = elem->next) {
    	    memset(acTemp, 0, 200);
			sprintf(acTemp, "%s,", elem->string);
			memcpy(pcKeys + uiKeysOffset, acTemp, strlen(acTemp));
			uiKeysOffset += strlen(acTemp);
			
			memset(acTemp, 0, 200);
			if(elem->type == cJSON_String) {
				sprintf(acTemp, "'%s',", elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, "'%f',", elem->valuedouble);
			} else {
				// wrong type, handle error
			}

			memcpy(pcValues + uiValuesOffset, acTemp, strlen(acTemp));
    	    uiValuesOffset += strlen(acTemp);
    	}
		memcpy(pcKeys + uiKeysOffset - 1, ") ", strlen(") "));
		uiKeysOffset += 1;
		pcValues[uiValuesOffset - 1] = ')';
		// 将两部分合为完整的SQL语句
		memcpy(pcInsertSql + uiOffset, "insert into relatedInfo ", strlen("insert into relatedInfo "));
		uiOffset += strlen("insert into relatedInfo ");
		memcpy(pcInsertSql + uiOffset, pcKeys, uiKeysOffset);
		uiOffset += uiKeysOffset;
		memcpy(pcInsertSql + uiOffset, pcValues, strlen(pcValues));
		uiOffset += uiValuesOffset;

		// 执行SQL语句
		printf("relatedInfo insert sql is %s\n", pcInsertSql);
		if(iMysqlErr == SUCCESS && mysql_query(pConn, pcInsertSql)) {
			fprintf(stderr, "%s\n", mysql_error(pConn));
			iMysqlErr = FAILURE;
		}
	 }

	 // 错误处理，向用户返回结果
	if(iMysqlErr == FAILURE) {
		pcErrDesc = mysql_error(pConn);
		sprintf(acBuff + sizeof(struct SEC_Header), pcErrDesc, strlen(pcErrDesc));
		stHeader.ucCmdRes = FAILURE;
		stHeader.uiLen = strlen(pcErrDesc);
		mysql_query(pConn, "ROLLBACK");
	} else if(iMysqlErr == SUCCESS) {
		mysql_query(pConn, "COMMIT");
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;
	}

	// 恢复自动提交
	mysql_query(pConn, "set autocommit = 1");

	// 发送操作结果
	memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
	if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
		printf("Lost connection with sec_gate, please check.\n");
	}
	// 释放内存
	cJSON_Delete(root);
	free(pcInsertSql);
	free(pcJson);
	free(pcKeys);
	free(pcValues);
}

/**
 * 函数功能：将二进制形式的文件数据插入到数据库表中，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_store_file(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;			// 数据包头
	struct SEC_TLV *pstTlv;				// TLV结构
	char *pcInsertSql;					// 存放拼接的sql语句
	char *pcFile;						// 存放请求中的文件数据
	char *pcKeys;						// 存放拼接sql语句时的列名
	char *pcValues;						// 存放拼接sql语句时的列值
	char acInvoiceID[40];				// 发票ID
	char acBuff[MAX_PKT_LEN];			// 存放结果数据包
	unsigned int uiOffset;				// pcInsertSql中的偏移值
	unsigned int uiKeysOffset;			// pcKeys中的偏移值
	unsigned int uiValuesOffset;		// pcValues中的偏移值
	const char *pcErrStr;				// MySQL出错原因描述
	
	// 初始化变量
	memset(acInvoiceID, 0, 100);
	memset(acBuff, 0, MAX_PKT_LEN);
	uiOffset = 0;
	uiKeysOffset = 0;
	uiValuesOffset = 0;
	// 填充数据包头
	stHeader.ucVersion = CUR_VERSION;
	stHeader.ucCmd = CMD_STORE_RES;  // 设置操作类型
	stHeader.iFd = pstHeader->iFd;  // 设置socket标识
	stHeader.ucIsLast = 0;

	// 分配内存并且清零
	pcInsertSql = (char*)malloc(MAX_INSERT_LINE);
	memset(pcInsertSql, 0, MAX_INSERT_LINE);
	pcFile = (char*)malloc(MAX_INSERT_LINE);
	memset(pcFile, 0, MAX_INSERT_LINE);
	pcValues = (char*)malloc(MAX_INSERT_LINE);
	memset(pcValues, 0, MAX_INSERT_LINE); 
	pcKeys = (char*)malloc(MAX_INSERT_LINE);
	memset(pcKeys, 0, MAX_INSERT_LINE);

	// 根据操作类型选择需要访问的数据库表
	switch(pstHeader->ucCmd) {
		case CMD_STORE_PDF :
			memcpy(pcInsertSql + uiOffset, "insert into pdfInfo ", strlen("insert into pdfInfo "));
			uiOffset += strlen("insert into pdfInfo ");
			break;
		case CMD_STORE_IMG :
			memcpy(pcInsertSql + uiOffset, "insert into imgInfo ", strlen("insert into imgInfo "));
			uiOffset += strlen("insert into imgInfo ");
			break;
		case CMD_STORE_OFD :
			memcpy(pcInsertSql + uiOffset, "insert into ofdInfo ", strlen("insert into ofdInfo "));
			uiOffset += strlen("insert into ofdInfo ");
			break;
		default :
			// to do
			break;
	}

	// 拼接sql语句
	memcpy(pcKeys + uiKeysOffset, "(", strlen("("));
	uiKeysOffset += strlen("(");
	memcpy(pcValues + uiValuesOffset, "values(", strlen("values("));
	uiValuesOffset += strlen("values(");
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
		if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID : // 发票ID部分
				memcpy(acInvoiceID, pcBuff, pstTlv->uiLen);
				memcpy(pcKeys + uiKeysOffset, "invoiceId,", strlen("invoiceId,"));
				uiKeysOffset += strlen("invoiceId,");
				sprintf(pcValues + uiValuesOffset, "'%s',", acInvoiceID);
				uiValuesOffset += (strlen(acInvoiceID) + 3);
				break;
			case FIELD_DATA : // 二进制数据部分
				mysql_real_escape_string(pConn, pcFile, pcBuff, pstTlv->uiLen);	// 二进制数据需要用该函数转化，转化后的长度最大会翻倍
				memcpy(pcKeys + uiKeysOffset, "data,", strlen("data,"));
				uiKeysOffset += strlen("data,");
				sprintf(pcValues + uiValuesOffset, "'%s',", pcFile);
				uiValuesOffset += (strlen(pcFile) + 3);
				break;
			default :
				break;
		}
		pcBuff += pstTlv->uiLen;
	}
	memcpy(pcKeys + uiKeysOffset - 1, ") ", strlen(") "));
	uiKeysOffset += strlen(") ") - 1;
	pcValues[uiValuesOffset - 1] = ')';
	memcpy(pcInsertSql + uiOffset, pcKeys, uiKeysOffset);
	uiOffset += uiKeysOffset;
	memcpy(pcInsertSql + uiOffset, pcValues, uiValuesOffset);
	uiOffset += uiValuesOffset;
	printf("file sql is %s\n", pcInsertSql);

	// 执行SQL语句
	mysql_ping(pConn);
	if (mysql_query(pConn, pcInsertSql)) {	// 操作失败
		pcErrStr =  mysql_error(pConn);
		sprintf(acBuff + sizeof(struct SEC_Header), pcErrStr, strlen(pcErrStr));
		//free(pcErrStr);
		stHeader.ucCmdRes = FAILURE;
		stHeader.uiLen = strlen(pcErrStr);
	} else {	// 操作成功
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;
	}

	// 发送操作结果
	memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
	if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
		printf("Lost connection with sec_gate, please check.\n");
	}

	// 释放内存
	free(pcInsertSql);
	free(pcFile);
	free(pcKeys);
	free(pcValues);
}

/**
 * 函数功能：更新发票的状态信息，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pstBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_update_status(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;			// 数据包头部
	struct SEC_TLV *pstTlv;				// TLV结构
	char *pcSql;						// 存放拼接的sql语句
	char *pcJson;						// 请求中的json数据
	char acInvoiceID[40];				// 发票ID
	char acBuff[MAX_PKT_LEN];			// 存放返回结果包
	cJSON *root;						// 将json字符串转换为cJSON结构
	cJSON *elem;						// json中的每个子项
	unsigned char temp[200];			// 临时存放json键值对
	unsigned int uiOffset;				// pcSql中的偏移
	const char *pucErrDesc;				// MySQL错误原因描述
	
	// 分配内存并清零
	memset(acInvoiceID, 0, 40);
	memset(acBuff, 0, MAX_PKT_LEN);
	pcSql = (char*)malloc(MAX_INSERT_LINE);
	memset(pcSql, 0, MAX_INSERT_LINE);
	pcJson = (char*)malloc(MAX_PKT_LEN);
	memset(pcJson, 0, MAX_PKT_LEN);

	// 填充数据包
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucCmd = CMD_UPDATE_RES;
	stHeader.ucIsLast = 1;
	uiOffset = 0;

	// 解析各TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
		if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID :
				memcpy(acInvoiceID, pcBuff, pstTlv->uiLen);
				break;
			case FIELD_JSON :
				memcpy(pcJson, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
		}
		pcBuff += pstTlv->uiLen;
	}
	sprintf(pcSql + uiOffset, "update basicInfo set ");
	uiOffset += strlen("update basicInfo set ");

	// 解析查询条件json字符串
	root = cJSON_Parse(pcJson);
	for(elem = root->child; elem != NULL; elem = elem->next) {
		memset(temp, 0, 200);
		if(strcmp(elem->string, "tax-refundStatus") == 0) { // 注意特殊字符要使用反单引号括起来
			if(elem->type == cJSON_String) {
				sprintf(temp, "`%s`='%s',", elem->string, elem->valuestring);
			} else if(elem->type == cJSON_Number){
				sprintf(temp, "`%s`='%f',", elem->string, elem->valuedouble);
			} else {
				//wrong type, handle error
			}
		} else if(strcmp(elem->string, "reimburseLockDate") == 0 || 
				strcmp(elem->string, "reimburseIssuDate") == 0 || strcmp(elem->string, "reimburseCanDate") == 0) { // 日期属性支持null值
			if(strcmp(elem->valuestring, "null") == 0) {
				sprintf(temp, "%s=%s,", elem->string, elem->valuestring);
			} else {
				sprintf(temp, "%s='%s',", elem->string, elem->valuestring);
			}
		} else {
			if(elem->type == cJSON_String) {
				sprintf(temp, "%s='%s',", elem->string, elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(temp, "%s='%f',", elem->string, elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		}

		memcpy(pcSql + uiOffset, temp, strlen(temp));
		uiOffset += strlen(temp);
	}
	pcSql[uiOffset - 1] = ' ';
	memset(temp, 0, 200);
	sprintf(temp, "where invoiceId='%s'", acInvoiceID);
	memcpy(pcSql + uiOffset, temp, strlen(temp));
	printf("update stauts sql is %s\n", pcSql);

	// 执行SQL语句
	mysql_ping(pConn);
	if(mysql_query(pConn, pcSql)) { // sql执行出错
		stHeader.ucCmdRes = FAILURE;
		pucErrDesc = mysql_error(pConn);
		memcpy(acBuff + sizeof(struct SEC_Header), pucErrDesc, strlen(pucErrDesc));
		stHeader.uiLen = strlen(pucErrDesc);
	} else if(mysql_affected_rows(pConn) > 0) {
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;
	} else { // 没有匹配的行 
		stHeader.ucCmdRes = FAILURE;
		stHeader.uiLen = 0;
	}
	// 发送操作结果
	memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
	if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
		printf("Lost connection with sec_gate, please check.\n");
	}

	// 释放内存
	cJSON_Delete(root);
	free(pcSql);
	free(pcJson);
}

/**
 * 函数功能：更新发票的所有权信息，并将操作结果返回给用户
 * 参数：
 *     @pConn[in]：MySQL句柄
 *     @pcBuff[in]：存放数据的缓冲区
 *     @iSockFd[in]：与上层节点通信的socket描述符
 *     @pstHeader[in]：数据包头
 * 返回值：无
 */
void SEC_storage_api_update_ownership(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;			// 数据包头部
	struct SEC_TLV *pstTlv;				// TLV结构
	char *pcSql;						// 存放拼接的sql语句
	char *pcJson;						// 请求中的json数据
	char acInvoiceID[40];				// 发票ID
	char acBuff[MAX_PKT_LEN];			// 存放返回结果包
	cJSON *root;						// 将json字符串转换为cJSON结构
	cJSON *elem;						// json中的每个子项
	unsigned char temp[200];			// 临时存放json键值对
	unsigned int uiOffset;				// pcSql中的偏移
	const char *pucErrDesc;				// MySQL错误原因描述
	
	// 分配内存并清零
	memset(acInvoiceID, 0, 40);
	memset(acBuff, 0, MAX_PKT_LEN);
	pcSql = (char*)malloc(MAX_INSERT_LINE);
	memset(pcSql, 0, MAX_INSERT_LINE);
	pcJson = (char*)malloc(MAX_PKT_LEN);
	memset(pcJson, 0, MAX_PKT_LEN);
	
	// 填充数据包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucCmd = CMD_UPDATE_RES;
	stHeader.ucIsLast = 1;
	uiOffset = 0;

	// 解析各个TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
		if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID :
				memcpy(acInvoiceID, pcBuff, pstTlv->uiLen);
				break;
			case FIELD_JSON :
				memcpy(pcJson, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
		}
		pcBuff += pstTlv->uiLen;
	}
	sprintf(pcSql + uiOffset, "update basicInfo set ");
	uiOffset += strlen("update basicInfo set ");

	// 解析json串，拼接sql语句
	root = cJSON_Parse(pcJson);
	for(elem = root->child; elem != NULL; elem = elem->next) {
		memset(temp, 0, 200);
		if(elem->type == cJSON_String) {
			sprintf(temp, "%s='%s',", elem->string, elem->valuestring);
		} else if(elem->type == cJSON_Number) {
			sprintf(temp, "%s='%f',", elem->string, elem->valuedouble);
		} else {
			// wrong type , handle error
		}
		memcpy(pcSql + uiOffset, temp, strlen(temp));
		uiOffset += strlen(temp);
	}
	pcSql[uiOffset - 1] = ' ';
	memset(temp, 0, 200);
	sprintf(temp, "where invoiceId='%s'", acInvoiceID);
	memcpy(pcSql + uiOffset, temp, strlen(temp));
	printf("update ownership sql is %s\n", pcSql);

	// 执行SQL语句
	mysql_ping(pConn);
	if(mysql_query(pConn, pcSql)) { // sql执行出错
		stHeader.ucCmdRes = FAILURE;
		pucErrDesc = mysql_error(pConn);
		memcpy(acBuff + sizeof(struct SEC_Header), pucErrDesc, strlen(pucErrDesc));
		stHeader.uiLen = strlen(pucErrDesc);
	} else if(mysql_affected_rows(pConn) > 0) {
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;
	} else { // 没有匹配的行
		stHeader.ucCmdRes = FAILURE;
		stHeader.uiLen = 0;
	}
	// 发送操作结果
	memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
	if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
		printf("Lost connection with sec_gate, please check.\n");
	}	

	// 释放内存
	cJSON_Delete(root);
	free(pcSql);
	free(pcJson);
}

 /**
  * 函数功能：更新发票的报销信息，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_update_reim(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;			// 数据包头部
	struct SEC_TLV *pstTlv;				// TLV结构
	char *pcSql;						// 存放拼接的sql语句
	char *pcJson;						// 请求中的json数据
	char acInvoiceID[40];				// 发票ID
	char acBuff[MAX_PKT_LEN];			// 存放返回结果包
	cJSON *root;						// 将json字符串转换为cJSON结构
	cJSON *elem;						// json中的每个子项
	unsigned char temp[200];			// 临时存放json键值对
	unsigned int uiOffset;				// pcSql中的偏移
	const char *pucErrDesc;				// MySQL错误原因描述
	
	// 分配内存并清零
	memset(acInvoiceID, 0, 40);
	memset(acBuff, 0, MAX_PKT_LEN);
	pcSql = (char*)malloc(MAX_INSERT_LINE);
	memset(pcSql, 0, MAX_INSERT_LINE);
	pcJson = (char*)malloc(MAX_PKT_LEN);
	memset(pcJson, 0, MAX_PKT_LEN);
	
	// 填充数据包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucCmd = CMD_UPDATE_RES;
	stHeader.ucIsLast = 1;
	uiOffset = 0;
	
	// 解析各个TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
		if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID :
				memcpy(acInvoiceID, pcBuff, pstTlv->uiLen);
				break;
			case FIELD_JSON :
				memcpy(pcJson, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
		}
		pcBuff += pstTlv->uiLen;
	}
	sprintf(pcSql + uiOffset, "update basicInfo set ");
	uiOffset += strlen("update basicInfo set ");

	// 解析json串，拼接sql语句
	root = cJSON_Parse(pcJson);
	for(elem = root->child; elem != NULL; elem = elem->next) {
		memset(temp, 0, 200);
		if(strcmp(elem->string, "reimburseLockDate") == 0 || 
					strcmp(elem->string, "reimburseIssuDate") == 0 || strcmp(elem->string, "reimburseCanDate") == 0) { // 日期类型属性支持null值
			if(strcmp(elem->valuestring, "null") == 0) {
				sprintf(temp, "%s=%s,", elem->string, elem->valuestring);
			} else {
				sprintf(temp, "%s='%s',", elem->string, elem->valuestring);
			}
		} else {
			if(elem->type == cJSON_String) {
				sprintf(temp, "%s='%s',", elem->string, elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(temp, "%s='%f',", elem->string, elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		}
		memcpy(pcSql + uiOffset, temp, strlen(temp));
		uiOffset += strlen(temp);
	}
	pcSql[uiOffset - 1] = ' ';
	memset(temp, 0, 200);
	sprintf(temp, "where invoiceId='%s'", acInvoiceID);
	memcpy(pcSql + uiOffset, temp, strlen(temp));
	printf("update reim sql is %s\n", pcSql);

	// 执行SQL语句
	mysql_ping(pConn);
	if(mysql_query(pConn, pcSql)) { // sql执行出错
		stHeader.ucCmdRes = FAILURE;
		pucErrDesc = mysql_error(pConn);
		memcpy(acBuff + sizeof(struct SEC_Header), pucErrDesc, strlen(pucErrDesc));
		stHeader.uiLen = strlen(pucErrDesc);
	} else if(mysql_affected_rows(pConn) > 0) {
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;
	} else { // 没有匹配的行 
		stHeader.ucCmdRes = FAILURE;
		stHeader.uiLen = 0;
	}
	// 发送操作结果
	memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
	if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
		printf("Lost connection with sec_gate, please check.\n");
	}

	// 释放内存
	cJSON_Delete(root);
	free(pcSql);
	free(pcJson);
}

 /**
  * 函数功能：根据发票ID的查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_id_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;		// 数据包头
	struct SEC_TLV *pstTlv;			// TLV结构
	char *pcQuerySql;				// 构造查询语句的缓冲区
	char acInvoiceId[40];			// 发票ID
	char acBuff[MAX_PKT_LEN];		// 构造返回结果的缓冲区
	cJSON *root;					// json结构的根结点
	cJSON *itemArray;				// json中item数组节点
	cJSON *relatedArray;			// json中related数组节点
	cJSON *item;					// cJSON结构的临时变量
	cJSON *temp;					// cJSON结构的临时变量
	MYSQL_RES *pResult1;			// basicInfo表的查询结果
	MYSQL_RES *pResult2;			// itemInfo表的查询结果
	MYSQL_RES *pResult3;			// relatedInfo表的查询结果
	MYSQL_ROW row;					// 每一条记录
	MYSQL_FIELD *fileds;			// 每一行记录的列数组
	int iSendLen;					// 记录要发送数据总长度
	int iDataLen;					// json字符串的长度
	unsigned int uiFiledsNum;		// 每一行记录有多少列
	int i;							// 循环标记
	int iErrFlag;					// 出错标志，1表示MySQL处理过程中出现了错误
	char *pcJsonStr;				// 将cJSON结构转为json字符串
	const char* pcErrDesc;			// MySQL错误原因描述

	// 初始化变量
	root = cJSON_CreateObject();
	pResult1 = NULL;
	pResult2 = NULL;
	pResult3 = NULL;
	iErrFlag = SUCCESS;

	// 分配内存并清零
	memset(acBuff, 0, MAX_PKT_LEN);
	memset(acInvoiceId, 0, 40);
	pcQuerySql = (char*)malloc(MAX_QUERY_LINE);
	memset(pcQuerySql, 0, MAX_QUERY_LINE);

	// 填充包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucIsLast = 0;

	// 解析TLV结构
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
	    if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID :
				memcpy(acInvoiceId, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
	}
		pcBuff += pstTlv->uiLen;
	}
	sprintf(pcQuerySql, "select * from basicInfo where invoiceId = '%s'", acInvoiceId);
	printf("basicInfo query sql is %s\n", pcQuerySql);
	
	mysql_ping(pConn);
	// 查询basicInfo表
	if (mysql_query(pConn, pcQuerySql)) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
	// 取出查询结果，方便进一步解析
	pResult1 = mysql_store_result(pConn);
	if (pResult1 == NULL) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}

	// 逐行解析数据
	iSendLen = 0;
	while(1) {
		// 取出每一行记录的列数组，以及列的个数
		fileds = mysql_fetch_fields(pResult1);
		uiFiledsNum = mysql_num_fields(pResult1);
		row = mysql_fetch_row(pResult1);
		if(row == NULL) {  // 判断记录是否解析完毕
			break;
		}

		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucIsLast = 0;  // 表明还有后续数据

		iSendLen += sizeof(struct SEC_Header);
			
		// 将查询结果按照规定格式放置于缓冲区中
		// 暂定每个查询结果单独发一个包回去
		for(i = 0; i < uiFiledsNum; i++) {
			item = cJSON_CreateString(row[i]);
			cJSON_AddItemToObject(root, fileds[i].name, item);
		}
		
		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from itemInfo where invoiceId = '%s'", acInvoiceId);
		//printf("itemInfo query sql is %s\n", pcQuerySql);

		// 查询itemInfo表
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult2 = mysql_store_result(pConn);
		// 判断是否有数据返回
		if (pResult2 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult2);
		uiFiledsNum = mysql_num_fields(pResult2);
		itemArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult2);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 跳过第1列和第2列
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(itemArray, temp);
		}
		cJSON_AddItemToObject(root, "issuItemInformation", itemArray);
		mysql_free_result(pResult2);

		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from relatedInfo where invoiceId = '%s'", acInvoiceId);
		//printf("relatedInfo query sql is %s\n", pcQuerySql);
		// 查询ralatedInfo
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult3 = mysql_store_result(pConn);
		if (pResult3 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult3);
		uiFiledsNum = mysql_num_fields(pResult3);
		relatedArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult3);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 跳过第1列和第2列
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(relatedArray, temp);
		}
		cJSON_AddItemToObject(root, "invoiceRelatedInformation", relatedArray);
		mysql_free_result(pResult3);

		// 处理错误情况
label:
		if(iErrFlag == SUCCESS) {
			pcJsonStr = cJSON_PrintUnformatted(root);
			iDataLen = strlen(pcJsonStr);
			memcpy(acBuff + iSendLen, pcJsonStr, iDataLen);
			iSendLen += iDataLen;
			free(pcJsonStr);

			stHeader.uiLen = iSendLen - sizeof(struct SEC_Header);
			stHeader.ucCmdRes = SUCCESS;
			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
			// 发送操作结果
			if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}         
			iSendLen = 0;
		} else if(iErrFlag == FAILURE) {
			stHeader.ucCmdRes = FAILURE;
			pcErrDesc = mysql_error(pConn);
			memcpy(acBuff + sizeof(struct SEC_Header), pcErrDesc, strlen(pcErrDesc));
			stHeader.uiLen = strlen(pcErrDesc);

			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
			// 发送操作结果
			if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}
			break;
		}
	}

	// 最后一个结果包
	if(iErrFlag == SUCCESS) {
		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucCmdRes = 0;
		stHeader.ucIsLast = 1;  
		stHeader.uiLen = 0;
		iSendLen += sizeof(struct SEC_Header);
		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
		iSendLen = 0;
	}

	if(root != NULL) {
		cJSON_Delete(root);
	}
	// 释放内存
	free(pcQuerySql);
	mysql_free_result(pResult1);
}

/**
  * 函数功能：根据多发票ID批量查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_id_batch_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader){
	struct SEC_Header stHeader;		// 数据包头
	struct SEC_TLV *pstTlv;			// TLV结构
	char *pcQuerySql;				// 构造查询语句的缓冲区
	char acInvoiceId[40];			// 发票ID
    char acTemp[200];				// 用于解析json中的每一个键值对
	char acBuff[MAX_PKT_LEN];		// 构造返回结果的缓冲区
	char acJson[MAX_PKT_LEN];		// 存放请求中的json字符串
	cJSON *oldRoot;					// 解析查询条件中的json字符串
	cJSON *root;					// json结构的根结点
	cJSON *itemArray;				// json中item数组节点
	cJSON *relatedArray;			// json中related数组节点
	cJSON *item;					// cJSON结构的临时变量
	cJSON *temp;					// cJSON结构的临时变量
	cJSON *invoiceIdList;			// 用于解析多id查询的情况
	cJSON *elem;					// 用于解析查询json中的每一个子项
	MYSQL_RES *pResult1;			// basicInfo表的查询结果
	MYSQL_RES *pResult2;			// itemInfo表的查询结果
	MYSQL_RES *pResult3;			// relatedInfo表的查询结果
	MYSQL_ROW row;					// 每一条记录
	MYSQL_FIELD *fileds;			// 每一行记录的列数组
	int iSendLen;					// 记录要发送数据总长度
	int iDataLen;					// json字符串的长度
	int invoiceIdListSize;			// 多id查询的id数量
	unsigned int uiOffset;			// pcQuerySql中的偏移
	unsigned int uiFiledsNum;		// 每一行记录有多少列
	int i;							// 循环标记
	int iErrFlag;					// 出错标志，1表示MySQL处理过程中出现了错误
	char *pcJsonStr;				// 将cJSON结构转为json字符串
	const char* pcErrDesc;			// MySQL错误原因描述

	// 初始化变量
	iErrFlag = SUCCESS;
	oldRoot = NULL;
	root = cJSON_CreateObject();
	invoiceIdList = NULL;
	pResult1 = NULL;
	pResult2 = NULL;
	pResult3 = NULL;
	uiOffset = 0;

	// 分配内存并清零
	pcQuerySql = (unsigned char*)malloc(MAX_QUERY_LINE);
	memset(pcQuerySql, 0, MAX_QUERY_LINE);

	// 填充包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucIsLast = 0;

	// 解析各个TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
	    if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_JSON :
				memcpy(acJson, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
	}
		pcBuff += pstTlv->uiLen;
	}

	sprintf(pcQuerySql, "select * from basicInfo where");
	uiOffset += strlen("select * from basicInfo where");
	printf("invoiceId batch query sql is %s\n", pcQuerySql);

	//解析查询条件中的invoiceId列表
	oldRoot = cJSON_Parse(acJson);

	invoiceIdList = cJSON_GetObjectItem(oldRoot, "invoiceIdList");
	if(invoiceIdList != NULL){
		invoiceIdListSize = cJSON_GetArraySize(invoiceIdList);
		memcpy(pcQuerySql + uiOffset, " invoiceId in (", strlen(" invoiceId in ("));
		uiOffset += strlen(" invoiceId in (");
		for(i = 0; i < invoiceIdListSize; i++) {
			memset(acTemp, 0, 200);
			// 获取数组中的一项
			item = cJSON_GetArrayItem(invoiceIdList, i);
			if(i != invoiceIdListSize-1) {
				sprintf(acTemp, " %s, ", item->valuestring);
			} else {
				sprintf(acTemp, " %s ", item->valuestring);
			}
			memcpy(pcQuerySql+uiOffset, acTemp, strlen(acTemp));
			uiOffset += strlen(acTemp);
		}
		memcpy(pcQuerySql + uiOffset, ") ", strlen(") "));
		uiOffset += strlen(") ");
	} else{
		// wrong type, handle error
	}
	printf("invoiceId batch query sql is %s\n", pcQuerySql);
	cJSON_Delete(oldRoot);
	
	mysql_ping(pConn);
	// 查询basicInfo
	if (mysql_query(pConn, pcQuerySql)) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
	// 取出查询结果，方便进一步解析
	pResult1 = mysql_store_result(pConn);
	if (pResult1 == NULL) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}

	// 逐行进行解析
	iSendLen = 0;
	while(1){
	// 取出每一行记录的列数组，以及列的个数
		fileds = mysql_fetch_fields(pResult1);
		uiFiledsNum = mysql_num_fields(pResult1);
		row = mysql_fetch_row(pResult1);
		if(row == NULL) {  // 判断记录是否解析完毕
			break;
		}
		memset(acTemp, 0, 200);
		memcpy(acTemp, row[0], strlen(row[0]));
		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucIsLast = 0;  // 表明还有后续数据

		iSendLen += sizeof(struct SEC_Header);
			
		// 将查询结果按照规定格式放置于缓冲区中
		// 暂定每个查询结果单独发一个包回去
		for(i = 0; i < uiFiledsNum; i++) {
			item = cJSON_CreateString(row[i]);
			cJSON_AddItemToObject(root, fileds[i].name, item);
		}
		
		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from itemInfo where invoiceId = '%s'", acTemp);
printf("query sql is %s\n", pcQuerySql);
		// 查询itemInfo表
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult2 = mysql_store_result(pConn);
		if (pResult2 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult2);
		uiFiledsNum = mysql_num_fields(pResult2);
		itemArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult2);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 从第三列开始取值
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(itemArray, temp);
		}
		cJSON_AddItemToObject(root, "issuItemInformation", itemArray);
		mysql_free_result(pResult2);

		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from relatedInfo where invoiceId = '%s'", acTemp);
printf("query sql is %s\n", pcQuerySql);
		// 查询relatedInfo表
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult3 = mysql_store_result(pConn);
		if (pResult3 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult3);
		uiFiledsNum = mysql_num_fields(pResult3);
		relatedArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult3);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 从第三列开始
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(relatedArray, temp);
		}
		cJSON_AddItemToObject(root, "invoiceRelatedInformation", relatedArray);
		mysql_free_result(pResult3);

	// 处理错误	
	label:
		if(iErrFlag == SUCCESS) {
			pcJsonStr = cJSON_PrintUnformatted(root);
			iDataLen = strlen(pcJsonStr);

			memcpy(acBuff + iSendLen, pcJsonStr, iDataLen);
			iSendLen += iDataLen;
			free(pcJsonStr);

			stHeader.uiLen = iSendLen - sizeof(struct SEC_Header);
			stHeader.ucCmdRes = SUCCESS;
			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));

			// 发送操作结果
			if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}         
			iSendLen = 0;
			if(root != NULL) {
				cJSON_Delete(root);
				root = cJSON_CreateObject();
			}
		} else if(iErrFlag == FAILURE) {
			stHeader.ucCmdRes = FAILURE;
			pcErrDesc = mysql_error(pConn);
			memcpy(acBuff + sizeof(struct SEC_Header), pcErrDesc, strlen(pcErrDesc));
			stHeader.uiLen = strlen(pcErrDesc);

			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
			// 发送操作结果
			if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}
			break;
		}
	}
	// 最后一个结果包
	if(iErrFlag == SUCCESS) {
		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucIsLast = 1; 
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;

		iSendLen += sizeof(struct SEC_Header);
		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
		iSendLen = 0;
	}

	if(root != NULL) {
		cJSON_Delete(root);
	}
	// 释放内存
	free(pcQuerySql);
	mysql_free_result(pResult1);

}


 /**
  * 函数功能：根据发票关键要素查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_elem_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;		// 数据包头
	struct SEC_TLV *pstTlv;			// TLV结构
	char *pcQuerySql;				// 构造查询语句的缓冲区
	char acInvoiceId[40];			// 发票ID
    char acTemp[200];				// 用于解析json中的每一个键值对
	char acBuff[MAX_PKT_LEN];		// 构造返回结果的缓冲区
	char acJson[MAX_PKT_LEN];		// 存放请求中的json字符串
	cJSON *oldRoot;					// 解析查询条件中的json字符串
	cJSON *root;					// json结构的根结点
	cJSON *itemArray;				// json中item数组节点
	cJSON *relatedArray;			// json中related数组节点
	cJSON *item;					// cJSON结构的临时变量
	cJSON *temp;					// cJSON结构的临时变量
	cJSON *elem;					// 用于解析查询json中的每一个子项
	MYSQL_RES *pResult1;			// basicInfo表的查询结果
	MYSQL_RES *pResult2;			// itemInfo表的查询结果
	MYSQL_RES *pResult3;			// relatedInfo表的查询结果
	MYSQL_ROW row;					// 每一条记录
	MYSQL_FIELD *fileds;			// 每一行记录的列数组
	int iSendLen;					// 记录要发送数据总长度
	int iDataLen;					// json字符串的长度
	unsigned int uiOffset;			// pcQuerySql中的偏移
	unsigned int uiFiledsNum;		// 每一行记录有多少列
	int i;							// 循环标记
	int iErrFlag;					// 出错标志，1表示MySQL处理过程中出现了错误
	char *pcJsonStr;				// 将cJSON结构转为json字符串
	const char* pcErrDesc;			// MySQL错误原因描述

	// 初始化变量
	iErrFlag = SUCCESS;
	oldRoot = NULL;
	root = cJSON_CreateObject();
	pResult1 = NULL;
	pResult2 = NULL;
	pResult3 = NULL;
	uiOffset = 0;

	// 分配内存并清零
	pcQuerySql = (unsigned char*)malloc(MAX_QUERY_LINE);
	memset(pcQuerySql, 0, MAX_QUERY_LINE);

	// 填充包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucIsLast = 0;
	
	// 解析各个TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
	    if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_JSON :
				memcpy(acJson, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
	}
		pcBuff += pstTlv->uiLen;
	}
	sprintf(pcQuerySql, "select * from basicInfo where");
	uiOffset += strlen("select * from basicInfo where");
	//printf("elem query sql is %s\n", pcQuerySql);
	
	oldRoot = cJSON_Parse(acJson);
    for(elem = oldRoot->child; elem != NULL; elem = elem->next) {
        memset(acTemp, 0, 200);
		if(strcmp(elem->string, "invoiceMoney") == 0) {
			// MySQL中，带横线的列名需要用反引号括起来，列值用单引号括起来
			if(elem->type == cJSON_String) {
				sprintf(acTemp, " %s='%s' and", "totalAmWithoutTax", elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, " %s='%f' and", "totalAmWithoutTax", elem->valuedouble);
			} else {
				// wrong type, handle error
			}
				
		} else if(strcmp(elem->string, "inIssuTime") == 0) {
			// datetime类型支持只按照日起查询
			sprintf(acTemp, " DATE_FORMAT(%s,\"%%Y%%m%%d\")=DATE_FORMAT(\"%s\", \"%%Y%%m%%d\") and", elem->string, elem->valuestring);
		} else if(strcmp(elem->string, "checkCode") == 0) {
			sprintf(acTemp, " %s like '%%%s' and", elem->string, elem->valuestring);
		} else {
			if(elem->type == cJSON_String) {
				sprintf(acTemp, " %s='%s' and", elem->string, elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, " %s='%f' and", elem->string, elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		}
        memcpy(pcQuerySql + uiOffset, acTemp, strlen(acTemp));
        uiOffset += strlen(acTemp);
    }
    pcQuerySql[uiOffset - 4] = '\0';
	printf("elem query sql is %s\n", pcQuerySql);
	cJSON_Delete(oldRoot);
	
	mysql_ping(pConn);
	// 查询basicInfo
	if (mysql_query(pConn, pcQuerySql)) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
	// 取出查询结果，方便进一步解析
	pResult1 = mysql_store_result(pConn);
	if (pResult1 == NULL) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
 
	// 逐行解析数据
	iSendLen = 0;
	while(1) {
		// 取出每一行记录的列数组，以及列的个数
		fileds = mysql_fetch_fields(pResult1);
		uiFiledsNum = mysql_num_fields(pResult1);
		row = mysql_fetch_row(pResult1);
		if(row == NULL) {  // 判断记录是否解析完毕
			break;
		}
		memset(acTemp, 0, 200);
		memcpy(acTemp, row[0], strlen(row[0]));
		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucIsLast = 0;  // 表明还有后续数据

		iSendLen += sizeof(struct SEC_Header);
			
		// 将查询结果按照规定格式放置于缓冲区中
		// 暂定每个查询结果单独发一个包回去
		for(i = 0; i < uiFiledsNum; i++) {
			item = cJSON_CreateString(row[i]);
			cJSON_AddItemToObject(root, fileds[i].name, item);
		}
		
		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from itemInfo where invoiceId = '%s'", acTemp);
		//	printf("query sql is %s\n", pcQuerySql);
		// 查询itemInfo表
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult2 = mysql_store_result(pConn);
		if (pResult2 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult2);
		uiFiledsNum = mysql_num_fields(pResult2);
		itemArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult2);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 从第三列开始取值
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(itemArray, temp);
		}
		cJSON_AddItemToObject(root, "issuItemInformation", itemArray);
		mysql_free_result(pResult2);

		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from relatedInfo where invoiceId = '%s'", acTemp);
		//	printf("query sql is %s\n", pcQuerySql);
		// 查询relatedInfo表
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult3 = mysql_store_result(pConn);
		if (pResult3 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult3);
		uiFiledsNum = mysql_num_fields(pResult3);
		relatedArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult3);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 从第三列开始
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(relatedArray, temp);
		}
		cJSON_AddItemToObject(root, "invoiceRelatedInformation", relatedArray);
		mysql_free_result(pResult3);

		// 处理错误
label :
		if(iErrFlag == SUCCESS) {
			pcJsonStr = cJSON_PrintUnformatted(root);
			iDataLen = strlen(pcJsonStr);
			memcpy(acBuff + iSendLen, pcJsonStr, iDataLen);
			iSendLen += iDataLen;
			free(pcJsonStr);

			stHeader.uiLen = iSendLen - sizeof(struct SEC_Header);
			stHeader.ucCmdRes = SUCCESS;
			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
			if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}         
			iSendLen = 0;
			if(root != NULL) {
				cJSON_Delete(root);
				root = cJSON_CreateObject();
			}
		} else if(iErrFlag == FAILURE) {
			stHeader.ucCmdRes = FAILURE;
			pcErrDesc = mysql_error(pConn);
			memcpy(acBuff + sizeof(struct SEC_Header), pcErrDesc, strlen(pcErrDesc));
			stHeader.uiLen = strlen(pcErrDesc);

			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));

			// 发送操作结果
			if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}
			break;
		}
	}

	// 最后一个结果包
	if(iErrFlag == SUCCESS) {
		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucIsLast = 1; 
		stHeader.ucCmdRes = SUCCESS;
		stHeader.uiLen = 0;

		iSendLen += sizeof(struct SEC_Header);
		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
		iSendLen = 0;
	}

	if(root != NULL) {
		cJSON_Delete(root);
	}
	// 释放内存
	free(pcQuerySql);
	mysql_free_result(pResult1);
}

 /**
  * 函数功能：根据发票普通字段查询数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_normal_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	struct SEC_Header stHeader;		// 数据包头
	struct SEC_TLV *pstTlv;			// TLV结构
	char *pcQuerySql;				// 构造查询语句的缓冲区
	char acInvoiceId[40];			// 发票ID
    char acTemp[200];				// 用于解析json中的每一个键值对
	char acBuff[MAX_PKT_LEN];		// 构造返回结果的缓冲区
	char acJson[MAX_PKT_LEN];		// 存放请求中的json字符串
	cJSON *oldRoot;					// 解析查询条件中的json字符串
	cJSON *root;					// json结构的根结点
	cJSON *itemArray;				// json中item数组节点
	cJSON *relatedArray;			// json中related数组节点
	cJSON *item;					// cJSON结构的临时变量
	cJSON *temp;					// cJSON结构的临时变量
	cJSON *elem;					// 用于解析查询json中的每一个子项
	MYSQL_RES *pResult1;			// basicInfo表的查询结果
	MYSQL_RES *pResult2;			// itemInfo表的查询结果
	MYSQL_RES *pResult3;			// relatedInfo表的查询结果
	MYSQL_RES *pResult4;			// 查询符合分页条件的总结果数
	MYSQL_ROW row;					// 每一条记录
	MYSQL_FIELD *fileds;			// 每一行记录的列数组
	int iSendLen;					// 记录要发送数据总长度
	int iDataLen;					// json字符串的长度
	//int iLimitOffset;				// 分页偏移位置
	int iNum;						// 页大小
	unsigned int uiTotalCount;		// 符合分页条件的结果总数
	unsigned int uiOffset;			// pcQuerySql中的偏移
	unsigned int uiFiledsNum;		// 每一行记录有多少列
	int i;							// 循环标记
	int iErrFlag;					// 出错标志，1表示MySQL处理过程中出现了错误
	char *pcJsonStr;				// 将cJSON结构转为json字符串
	const char* pcErrDesc;			// MySQL错误原因描述

	char *pcLastID;
	char *pcLastTime;

	char *submitterNum;				//保存查询条件中的submitterNum值
	char *invOwnNum;				//保存查询条件中的invOwnNum值
	
	// 初始化变量
	iErrFlag = SUCCESS;
	oldRoot = NULL;
	root = cJSON_CreateObject();
	pResult1 = NULL;
	pResult2 = NULL;
	pResult3 = NULL;
	pResult4 = NULL;
	uiOffset = 0;
	//iLimitOffset = 0;	// 默认从第一行开始
	iNum = -1;			// 默认到最后一行
	uiTotalCount = 0;
	pcQuerySql = (char*)malloc(MAX_QUERY_LINE);
	memset(pcQuerySql, 0, MAX_QUERY_LINE);
	pcLastID = NULL;
	pcLastTime = NULL;
	
	submitterNum = NULL;
	invOwnNum = NULL;

	// 填充包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));
	stHeader.ucIsLast = 0;

	// 解析各个TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
	    if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_JSON :
				memcpy(acJson, pcBuff, pstTlv->uiLen);
				break;
			//case FIELD_LIMIT_OFFSET :
			//	memcpy(&iLimitOffset, pcBuff, pstTlv->uiLen);
			//	break;
			case FIELD_LIMIT_NUM :
				memcpy(&iNum, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
	}
		pcBuff += pstTlv->uiLen;
	}

	sprintf(pcQuerySql, "select sql_calc_found_rows * from basicInfo where");
	uiOffset += strlen("select sql_calc_found_rows * from basicInfo where");
	//printf("normal query sql is %s\n", pcQuerySql);

	// 解析json字符串中的查询条件，拼接成sql语句
	oldRoot = cJSON_Parse(acJson);

	// 查看查询条件是否包含submitterNum且不包含invOwnNum
	preCheck(oldRoot, &submitterNum);

    for(elem = oldRoot->child; elem != NULL; elem = elem->next) {
        memset(acTemp, 0, 200);
		if(strcmp(elem->string, "lastID") == 0) {
			pcLastID = elem->valuestring;
		} else if(strcmp(elem->string, "lastTime") == 0) {
			pcLastTime = elem->valuestring;
		} else if(strcmp(elem->string, "buyerName") == 0 || strcmp(elem->string, "sellerName") == 0) {
			sprintf(acTemp, " %s like '%%%s%%' and", elem->string, elem->valuestring);
		} else if(strcmp(elem->string, "invOwnE-mail") == 0 || strcmp(elem->string, "tax-refundStatus") == 0) {
			// 带横线的列名需要用反引号括起来
			if(elem->type == cJSON_String) {
				sprintf(acTemp, " `%s`='%s' and", elem->string, elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, " `%s`='%f' and", elem->string, elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		} else if(strcmp(elem->string, "inIssuTimeBegin") == 0) {
			sprintf(acTemp, " inIssuTime >= '%s' and", elem->valuestring);
		} else if(strcmp(elem->string, "inIssuTimeEnd") == 0) {
			sprintf(acTemp, " inIssuTime <= '%s' and", elem->valuestring);
		} else if(strcmp(elem->string, "totalTaxIncludedAmMin") == 0) {
			if(elem->type == cJSON_String) {
				sprintf(acTemp, " `totalTax-includedAm` >= %s and", elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, " `totalTax-includedAm` >= %f and", elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		} else if(strcmp(elem->string, "totalTaxIncludedAmMax") == 0) {
			if(elem->type == cJSON_String) {
				sprintf(acTemp, " `totalTax-includedAm` <= %s and", elem->valuestring);
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, " `totalTax-includedAm` <= %f and", elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		} else if(strcmp(elem->string, "reimburseLockDate") == 0) {
			if(strcmp(elem->valuestring, "null") == 0 || strcmp(elem->valuestring, "not null") == 0) {
				sprintf(acTemp, " %s is %s and", elem->string, elem->valuestring);
			} else {
				sprintf(acTemp, " %s='%s' and", elem->string, elem->valuestring);
			}
		} else {
			if(elem->type == cJSON_String) {
				if(strcmp(elem->string, C_SUBMITTERNUM) == 0 && submitterNum != NULL) {
					sprintf(acTemp, " %s='%s' and %s!='%s' and", elem->string, elem->valuestring, C_INVOWNNUM, elem->valuestring);
				} else {
					sprintf(acTemp, " %s='%s' and", elem->string, elem->valuestring);

				}
			} else if(elem->type == cJSON_Number) {
				sprintf(acTemp, " %s='%f' and", elem->string, elem->valuedouble);
			} else {
				// wrong type, handle error
			}
		}
        memcpy(pcQuerySql + uiOffset, acTemp, strlen(acTemp));
        uiOffset += strlen(acTemp);
    }
	pcQuerySql[uiOffset - 1] = ' ';
	pcQuerySql[uiOffset - 2] = ' ';
	pcQuerySql[uiOffset - 3] = ' ';

	// 利用上次的查询结果进行分页 
	if(pcLastTime != NULL && pcLastID != NULL) {
		memset(acTemp, 0, 200);
		sprintf(acTemp, " and (inIssuTime < '%s' or (inIssuTime = '%s' and invoiceId < '%s'))", pcLastTime, pcLastTime, pcLastID);
		memcpy(pcQuerySql + uiOffset, acTemp, strlen(acTemp));
		uiOffset += strlen(acTemp);
	}

	// 初次进行分页查询时使用下面的语句
    memset(acTemp, 0, 200);
    sprintf(acTemp, " order by inIssuTime desc,invoiceId desc limit %d", iNum);
    memcpy(pcQuerySql + uiOffset, acTemp, strlen(acTemp));
	printf("normal query sql is %s\n", pcQuerySql);
	
	mysql_ping(pConn);
	// 查询basicInfo
	if (mysql_query(pConn, pcQuerySql)) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
	// 取出查询结果，方便进一步解析
	pResult1 = mysql_store_result(pConn);
	if (pResult1 == NULL) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}

	// 查询符合条件的总结果数
	if (mysql_query(pConn, "select found_rows();")) {
		finish_with_error(pConn);
		iErrFlag = 1;
		goto label;
	}
	// 取出查询结果，方便进一步解析
	pResult4 = mysql_store_result(pConn);
	if (pResult4 == NULL) {
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
	row = mysql_fetch_row(pResult4);
	if(row == NULL) {  // 判断记录是否解析完毕
		finish_with_error(pConn);
		iErrFlag = FAILURE;
		goto label;
	}
	//printf("*******%s\n", row[0]);
	iSendLen = sizeof(struct SEC_Header);
	sprintf(acBuff + iSendLen, "%u", atoi(row[0]));
	iSendLen += sizeof(unsigned int);
	stHeader.ucIsLast = 0;
	stHeader.ucCmdRes = SUCCESS;
	stHeader.ucCmd= CMD_NORMAL_COUNT;
	stHeader.uiLen = sizeof(unsigned int);
	memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
	if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
		printf("Lost connection with sec_gate, please check.\n");
	}
 
	// 逐行解析数据
	iSendLen = 0;
	while(1) {
		
		// 取出每一行记录的列数组，以及列的个数
		fileds = mysql_fetch_fields(pResult1);
		uiFiledsNum = mysql_num_fields(pResult1);
		row = mysql_fetch_row(pResult1);
		if(row == NULL) {  // 判断记录是否解析完毕
			break;
		}
		memset(acTemp, 0, 200);
		memcpy(acTemp, row[0], strlen(row[0]));
		stHeader.ucCmd = CMD_QUERY_RES;  // 操作类型
		stHeader.ucIsLast = 0;  // 表明还有后续数据

		iSendLen += sizeof(struct SEC_Header);
			
		// 将查询结果按照规定格式放置于缓冲区中
		// 暂定每个查询结果单独发一个包回去
		for(i = 0; i < uiFiledsNum; i++) {
			item = cJSON_CreateString(row[i]);
			cJSON_AddItemToObject(root, fileds[i].name, item);
		}
		
		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from itemInfo where invoiceId = '%s'", acTemp);
		//	printf("query sql is %s\n", pcQuerySql);
		// 查询ItemInfo
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult2 = mysql_store_result(pConn);
		if (pResult2 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult2);
		uiFiledsNum = mysql_num_fields(pResult2);
		itemArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult2);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 从第三列开始
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(itemArray, temp);
		}
		cJSON_AddItemToObject(root, "issuItemInformation", itemArray);
		mysql_free_result(pResult2);

		memset(pcQuerySql, 0, MAX_QUERY_LINE);
		sprintf(pcQuerySql, "select * from relatedInfo where invoiceId = '%s'", acTemp);
		//	printf("query sql is %s\n", pcQuerySql);
		// 查询relatedInfo表
		if (mysql_query(pConn, pcQuerySql)) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出查询结果，方便进一步解析
		pResult3 = mysql_store_result(pConn);
		if (pResult3 == NULL) {
			finish_with_error(pConn);
			iErrFlag = FAILURE;
			goto label;
		}
		// 取出每一行记录的列数组，以及列的个数，将每一行作为json数组的一个成员
		fileds = mysql_fetch_fields(pResult3);
		uiFiledsNum = mysql_num_fields(pResult3);
		relatedArray = cJSON_CreateArray();
		while(1) {
			row = mysql_fetch_row(pResult3);
			if(row == NULL) {
				break;
			}
			temp = cJSON_CreateObject();

			// 从第三列开始
			for(i = 2; i < uiFiledsNum; i++) {
				item = cJSON_CreateString(row[i]);
				cJSON_AddItemToObject(temp, fileds[i].name, item);
			}
			cJSON_AddItemToArray(relatedArray, temp);
		}
		cJSON_AddItemToObject(root, "invoiceRelatedInformation", relatedArray);
		mysql_free_result(pResult3);

		// 处理错误情形
label :
		if(iErrFlag == SUCCESS) {
			pcJsonStr = cJSON_PrintUnformatted(root);
			iDataLen = strlen(pcJsonStr);
			memcpy(acBuff + iSendLen, pcJsonStr, iDataLen);
			iSendLen += iDataLen;
			free(pcJsonStr);

			stHeader.uiLen = iSendLen - sizeof(struct SEC_Header);
			stHeader.ucCmdRes = SUCCESS;
			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
			if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}         
			iSendLen = 0;
			if(root != NULL) {
				cJSON_Delete(root);
				root = cJSON_CreateObject();
			}
		} else if(iErrFlag == FAILURE) {
			stHeader.ucCmdRes = FAILURE;
			pcErrDesc = mysql_error(pConn);
			memcpy(acBuff + sizeof(struct SEC_Header), pcErrDesc, strlen(pcErrDesc));
			stHeader.uiLen = strlen(pcErrDesc);

			memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));

			// 发送操作结果
			if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
				printf("Lost connection with sec_gate, please check.\n");
			}
			break;
		}
	}

	// 最后一个结果包
	if(iErrFlag == SUCCESS) {
		stHeader.ucCmd = CMD_QUERY_RES;
		stHeader.ucIsLast = 1;
		stHeader.ucCmd = SUCCESS;
		stHeader.uiLen = 0;

		iSendLen += sizeof(struct SEC_Header);
		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
		iSendLen = 0;
	}

	// 释放动态分配的内存，防止内存泄露 
	if(root != NULL) {
		cJSON_Delete(root);
		root = NULL;
	}
	cJSON_Delete(oldRoot);
	free(pcQuerySql);
	mysql_free_result(pResult1);
	mysql_free_result(pResult4);
}

 /**
  * 函数功能：根据发票ID查询文件数据，并将操作结果返回给用户
  * 参数：
  *     @pConn[in]：MySQL句柄
  *     @pcBuff[in]：存放数据的缓冲区
  *     @iSockFd[in]：与上层节点通信的socket描述符
  *     @pstHeader[in]：数据包头
  * 返回值：无
  */
void SEC_storage_api_mysql_file_query(MYSQL *pConn, char *pcBuff, int iSockFd, struct SEC_Header *pstHeader) {

	char *pcQuerySql;					// 存放查询语句
	char acInvoiceId[40];				// 存放发票ID
	struct SEC_TLV *pstTlv;				// 解析TLV结构
	MYSQL_RES *pResult;					// MySQL查询结果
	MYSQL_ROW row;						// 表示查询结果中的一行记录
	char acBuff[MAX_PKT_LEN];			// 数据缓冲区
	int iSendLen;						// 记录要发送数据总长度
	int iDataLen;						// 数据长度
	struct SEC_Header stHeader;			// 数据包头
	long *lengths;						// 查询结果中各个字段的长度
	int iMysqlErr;						// MySQL错误标志，表示在处理的过程中是否有错，1表示有错
	int iMallocErr;						// Malloc错误标志，表示在处理的过程中是否有错，1表示有错
	const char *pucErrDesc;				// 错误原因描述

	// 初始化变量
	iMysqlErr = SUCCESS;
	iMallocErr = SUCCESS;
	memset(acInvoiceId, 0, 40);

	// 分配内存空间
	pcQuerySql = (unsigned char*)malloc(MAX_QUERY_LINE);
	memset(pcQuerySql, 0, MAX_QUERY_LINE);

	// 填充数据包头
	memset(&stHeader, 0, sizeof(struct SEC_Header));
	memcpy(&stHeader, pstHeader, sizeof(struct SEC_Header));

	// 解析TLV字段
	while(1) {
		pstTlv = (struct SEC_TLV*)pcBuff;
	    if(pstTlv->ucType == FIELD_EOF){
			break;
		}
		pcBuff += sizeof(struct SEC_TLV);
		switch(pstTlv->ucType) {
			case FIELD_ID :
				memcpy(acInvoiceId, pcBuff, pstTlv->uiLen);
				break;
			default :
				break;
	}
		pcBuff += pstTlv->uiLen;
	}
	
	// 根据请求类型构造对应的查询语句
	if(stHeader.ucCmd == CMD_DOWNLOAD_PDF) {
		sprintf(pcQuerySql, "select data from pdfInfo where invoiceId = '%s'", acInvoiceId);
	} else if(stHeader.ucCmd == CMD_DOWNLOAD_IMG) {
		sprintf(pcQuerySql, "select data from imgInfo where invoiceId = '%s'", acInvoiceId);
	} else if(stHeader.ucCmd == CMD_DOWNLOAD_OFD) {
		sprintf(pcQuerySql, "select data from ofdInfo where invoiceId = '%s'", acInvoiceId);
	}
	printf("file query sql is %s\n", pcQuerySql);

	mysql_ping(pConn);
	// 查询数据库
	if (mysql_query(pConn, pcQuerySql)) {
		finish_with_error(pConn);
		iMysqlErr = FAILURE;
		goto label;
	}
	// 取出查询结果，方便进一步解析
	pResult = mysql_store_result(pConn);
 
	// 判断是否有数据返回
	if (pResult == NULL) {
		finish_with_error(pConn);
		iMysqlErr = FAILURE;
		goto label;
	}

	// 逐行解析数据
	iSendLen = 0;
	while(1) {
		// 取出一行记录
		row = mysql_fetch_row(pResult);
		lengths = mysql_fetch_lengths(pResult);
		if(row == NULL) {  // 判断记录是否解析完毕
			break;
		}
		stHeader.ucCmd = CMD_DOWNLOAD_RES;  // 操作类型
		stHeader.ucIsLast = 0;  // 表明还有后续数据

		iSendLen += sizeof(struct SEC_Header);
			
		// 将查询结果按照规定格式放置于缓冲区中
		// 暂定每个查询结果单独发一个包回去
		iDataLen = lengths[0];
		memcpy(acBuff + iSendLen, row[0], iDataLen);
		iSendLen += iDataLen;

		stHeader.uiLen = iSendLen - sizeof(struct SEC_Header);
		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
		iSendLen = 0;
	}

	// 根据错误标志进一步处理
label :
	if(iMysqlErr == SUCCESS) {
		// 填充包头和数据部分
		stHeader.ucCmd = CMD_DOWNLOAD_RES;  // 操作类型
		stHeader.ucCmdRes = SUCCESS;
		stHeader.ucIsLast = 1;
		stHeader.uiLen = 0;

		iSendLen += sizeof(struct SEC_Header);
		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, iSendLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
	} else if(iMysqlErr == FAILURE) {
		// 填充包头和数据部分
		stHeader.ucCmd = CMD_DOWNLOAD_RES;  // 操作类型
		stHeader.ucIsLast = 1;
		stHeader.ucCmdRes = FAILURE;
		pucErrDesc = mysql_error(pConn);
		memcpy(acBuff + sizeof(struct SEC_Header), pucErrDesc, strlen(pucErrDesc));
		stHeader.uiLen = strlen(pucErrDesc);

		memcpy(acBuff, &stHeader, sizeof(struct SEC_Header));
		if(SEC_storage_api_sendEx(iSockFd, acBuff, sizeof(struct SEC_Header) + stHeader.uiLen) == FAILURE) {
			printf("Lost connection with sec_gate, please check.\n");
		}
	}

	// 释放内存
	free(pcQuerySql);
	mysql_free_result(pResult);
}
