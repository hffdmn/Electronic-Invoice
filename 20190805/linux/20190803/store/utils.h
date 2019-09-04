#ifndef _UTILS_H
#define _UTILS_H

/**
 * 函数功能：接收指定长度的数据，最终接收到的数据大于等于这个长度
 * 参数：
 *     @iFd[in]：socket描述符
 *     @pucData[in]：数据缓冲区
 *     @piTotal[in]：接收数据的起始偏移位置
 *     @iDataLen[in]：指定的数据长度
 * 返回值：成功返回SUCCESS，失败返回其他值
 */
int SEC_storage_api_recvEx(int iFd, unsigned char *pucData, int *piTotal, int iDataLen);

/**
 * 函数功能：发送指定长度的数据
 * 参数：
 *     @iFd[in]：socket描述符
 *     @pucData[in]：数据缓冲区
 *     @iDataLen[in]：数据长度
 * 返回值：成功返回SUCCESS，失败返回FAILURE
 */
int SEC_storage_api_sendEx(int iFd, unsigned char *pucData, int iDataLen);

/*
 * 函数功能：计算字符串的hash值，java自带的字符串哈希函数
 * 输入：
 *     @pcStr[in]：用于哈希计算的字符串
 * 输出：哈希计算结果
 */
unsigned int SEC_storage_api_jHash(const char *pcStr);

/**
 * 函数功能：计算哈希值，SDBMHash
 * 输入：
 *     @pcStr[in]：进行哈希计算的字符串
 * 输出：哈希计算结果
 */
unsigned int SEC_storage_api_sdbmHash(const char *pcStr);

/**
 * 函数功能：计算哈希值，ELFHash
 * 输入：
 *     @pcStr[in]：进行哈希计算的字符串
 * 输出：哈希计算结果
 */
unsigned int SEC_storage_api_hash(const char *pcStr);

/*
 * 函数功能：生成32字节随机ID
 * 参数：无
 * 返回值：32字节随机ID字符串
 */
char *SEC_storage_api_randomID();

/*
 * 函数功能：返回当前毫秒时间，Linux下与Widnows下实现方式不一样
 * 参数：无
 * 返回值：时间的毫秒值
 */
long long SEC_storage_api_timestamp();

#endif
