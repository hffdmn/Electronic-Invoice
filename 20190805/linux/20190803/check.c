
/**
 *	函数功能：检测输入的查询条件中是否包含submitterNum和invOwnNum：
 * 	1. 如果查询条件只包含submitterNum，则需过滤掉submitterNum=invOwnNum情况，保留submitterNum的值
 *	2. 如果查询条件只包含invOwnNum，则不进行任何操作，返回SUCCESS
 *	3. 如果查询条件同时包含submitterNum和invOwnNum，，返回FAILURE
 *  备注：后续会根据是否得到了submitterNum进行判断是否执行过滤操作，所以会对该变量进行操作
 * 参数：
 *      @json[in]：查询条件组成的json对象
 * 		@submitterNum[out]: 报销人编号
 * 
 * 返回值：无特殊意义，返回SUCCESS时不用对submitterNum置空，返回FAILURE时相反
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

if(*submitterNum != NULL) {
	printf("[I] 查询条件包含submitterNum：%s\n", *submitterNum);
}

	if(*submitterNum && invOwnNum) {
printf("[I] 查询条件包含invOwnNum：%s, 无过滤\n", invOwnNum);
		return FAILURE;
	}
	return SUCCESS;
}






//	查询submitterNum时，若submitterNum为查询条件，且invOwnNum不为查询条件时
				//	过滤掉查询结果中满足invOwnNum==submitterNum的条目
				if(submitterNum != NULL) {
printf("[-]查询条件包含submitterNum：%s\n", submitterNum);
					invOwnNum = cJSON_GetStringValue(cJSON_GetObjectItem(json, "invOwnNum"));
printf("[-]该条目包含invOwnNum：%s\n", invOwnNum);

					if(invOwnNum!=NULL && !strcmp(submitterNum, invOwnNum)){
						subeqINVNum++;
printf("[%d]submitterNum与invOwnNum相等：{%d/%d}\n",i, subeqINVNum, uiCount - iOffset);
						continue;
					}
				}