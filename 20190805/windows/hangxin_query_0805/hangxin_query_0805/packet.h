#ifndef _PACKET_H
#define _PACKET_H

// 数据包最大长度 
#define MAX_PKT_LEN (128 * 1024)

// TLV字段类型
#define FIELD_ID 1				// 发票ID字段
#define FIELD_JSON 2			// json类型发票数据
#define FIELD_DATA 3			// 文件类型发票数据
#define FIELD_LIMIT_OFFSET 4	// 页查询偏移位置
#define FIELD_LIMIT_NUM 5		// 查询结果数
#define FIELD_EOF 6				// 标志TLV解析结束

// 当前数据包版本
#define CUR_VERSION 1

// 请求操作类型，存储相关
#define CMD_STORE_JSON 1	// 存储json数据
#define CMD_STORE_PDF 2		// 存储pdf文件
#define CMD_STORE_IMG 3		// 存储图片文件
#define CMD_STORE_OFD 4		// 存储ofd文件
#define CMD_STORE_RES 5		// 存储结果

// 请求操作类型，更新相关
#define CMD_STATUS_UPDATE 6		// 更新发票状态信息
#define CMD_OWNERSHIP_UPDATE 7	// 更新发票所有权信息
#define CMD_REIM_UPDATE 8		// 更新发票报销信息
#define CMD_UPDATE_RES 9		// 更新结果

// 请求操作类型，查询相关
#define CMD_ID_QUERY 10			// 根据发票ID查询
#define CMD_IDBATCH_QUERY 19	// 根据多ID批量查询		
#define CMD_ELEM_QUERY 11		// 根据发票关键要素查询
#define CMD_NORMAL_QUERY 12		// 根据一般元素查询
#define CMD_NORMAL_COUNT 13		// 分页查询总结果数
#define CMD_QUERY_RES 14		// 查询结果

// 请求操作类型，下载相关
#define CMD_DOWNLOAD_PDF 15		// 下载PDF文件
#define CMD_DOWNLOAD_IMG 16		// 下载图片文件
#define CMD_DOWNLOAD_OFD 17		// 下载OFD文件
#define CMD_DOWNLOAD_RES 18		// 下载结果

// 操作执行结果
#define RES_SUCCESS 0
#define RES_FAILURE 1

// 数据包头部
// pointer1，pointer2和pointer3执行的内存中中存放关于操作状态信息，在查询和更新操作时，
// 数据包可能会发送给多个节点，可以根据状态信息判断是否所有节点都成功返回了结果，
// 然后进行相应的处理
struct SEC_Header {
    unsigned char ucVersion;    // 版本号
    unsigned char ucCmd;        // 主命令码，标识操作类型
    unsigned char ucIsLast;     // 标记是否是最后一个数据包，1表示最后一个数据包
    unsigned char ucCmdRes;     // RES_SUCCESS表示成功，RES_FAILURE表示失败
    int iFd;                    // socket句柄，用于区分用户连接
    void *pointer1;
    void *pointer2;
    void *pointer3;
    unsigned int uiLen;         // 数据长度
};

// TLV结构体
struct SEC_TLV {
    unsigned char ucType;       // 字段类型
    unsigned int uiLen;         // 数据长度
    unsigned char aucData[0];   // 数据部分
};

#endif
