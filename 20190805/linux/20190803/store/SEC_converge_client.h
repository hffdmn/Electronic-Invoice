#ifndef SEC_CONVERGE_CLIENT_H
#define SEC_CONVERGE_CLIENT_H

/*
函数功能：初始化句柄
函数参数：
	@ip[in]			服务器IP地址
	@port[in]		服务器端口号
函数返回值：
	0 成功
	非0 失败
*/
int SEC_converge_client_init(
	char *pcIp, 
	unsigned short usPort
);

/**
函数功能：数据传输，将输入写入邮槽
函数参数：
		@pcInfo[in] 待传输的信息
函数返回值：
		0 成功
		-1 失败
**/
int SEC_converge_client_send(
	char *pcInfo,
	int iLen
);

/**
函数功能：释放句柄，结束线程
函数参数：
函数返回值：
		0 成功
		非0 失败
**/
void SEC_converge_client_close();

#endif
