#ifndef SEC_CONVERGE_CLIENT_H
#define SEC_CONVERGE_CLIENT_H

/*
�������ܣ���ʼ�����
����������
	@ip[in]			������IP��ַ
	@port[in]		�������˿ں�
��������ֵ��
	0 �ɹ�
	��0 ʧ��
*/
int SEC_converge_client_init(
	char *pcIp, 
	unsigned short usPort
);

/**
�������ܣ����ݴ��䣬������д���ʲ�
����������
		@pcInfo[in] ���������Ϣ
��������ֵ��
		0 �ɹ�
		-1 ʧ��
**/
int SEC_converge_client_send(
	char *pcInfo,
	int iLen
);

/**
�������ܣ��ͷž���������߳�
����������
��������ֵ��
		0 �ɹ�
		��0 ʧ��
**/
void SEC_converge_client_close();

#endif
