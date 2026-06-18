#include "adt_chain.h"

adt_chain_t* creat_chain(int memx,int* code)
{
	if(code == NULL)
	{
		return NULL;
	}
	
	osKernelLock();
	
	adt_chain_t* adt_chain_head = (adt_chain_t*)sys_malloc(memx,sizeof(adt_chain_t));
	if(adt_chain_head == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}
	
	adt_chain_head->next = NULL;
	adt_chain_head->data = NULL;
	adt_chain_head->memx = memx;
	
	osKernelUnlock();
	
	*code = SYSTEM_STATUS_OK;
	return adt_chain_head;
}

adt_chain_t* insert_tail_chain(adt_chain_t* adt_chain_begin,void* data,int* code)
{
	if(code == NULL)
	{
		return NULL;
	}
	
	if(adt_chain_begin == NULL || data == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}
	
	osKernelLock();
	
	adt_chain_t* pointer = adt_chain_begin;
	adt_chain_t* temp = (adt_chain_t*)sys_malloc(adt_chain_begin->memx,sizeof(adt_chain_t));
	
	temp->next = NULL;
	temp->data = data;
	temp->memx = adt_chain_begin->memx;
	
	while(pointer->next != NULL)
	{
		pointer = pointer->next;
	}
	
	pointer->next = temp;
	
	osKernelUnlock();
	
	*code = SYSTEM_STATUS_OK;
	return adt_chain_begin;
}

adt_chain_t* insert_head_chain(adt_chain_t* adt_chain_begin,void* data,int* code)
{
	if(adt_chain_begin == NULL || data == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}
	
	osKernelLock();
	
	adt_chain_t* temp = (adt_chain_t*)sys_malloc(adt_chain_begin->memx,sizeof(adt_chain_t));
	if(temp == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}
	
	temp->next = NULL;
	temp->data = data;
	temp->memx = adt_chain_begin->memx;

	temp->next = adt_chain_begin->next;

	adt_chain_begin->next = temp;
	
	osKernelUnlock();
	
	*code = SYSTEM_STATUS_OK;
	return adt_chain_begin;
}

adt_chain_t* delete_node(adt_chain_t* adt_chain_begin, void* data,int* code)
{
	if(adt_chain_begin == NULL || data == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}
	
	osKernelLock();

	adt_chain_t* prev = adt_chain_begin;  
	adt_chain_t* curr = adt_chain_begin->next;  

	while(curr != NULL)
	{
		if(curr->data == data)
		{
			prev->next = curr->next;  
			sys_free(curr->memx, curr);
			osKernelUnlock();
			*code = SYSTEM_STATUS_OK;
			return adt_chain_begin;
		}

		prev = curr;
		curr = curr->next;
	}
	
	osKernelUnlock();

	*code = OPERATE_FAILED;
	return adt_chain_begin;
}

adt_chain_t* destroy_chain(adt_chain_t* adt_chain_begin,int* code)
{
	if(adt_chain_begin == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}
	
	osKernelLock();

	adt_chain_t* pointer = adt_chain_begin;
	adt_chain_t* temp = NULL;

	while(pointer != NULL)
	{
		temp = pointer->next;		
		sys_free(pointer->memx, pointer);	
		pointer = temp;			
	}
	
	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return NULL;
}

int traverse_chain(adt_chain_t* adt_chain_begin, void** pbuf, int buf_size, int* code)
{
    // 入参合法性检查
    if (code == NULL)
    {
        return 0;
    }

    if (adt_chain_begin == NULL || pbuf == NULL || buf_size <= 0)
    {
        *code = OPERATE_FAILED;
        return 0;
    }

    osKernelLock();  // 线程安全加锁

    int count = 0;
    // 跳过头节点（你的设计是头结点不存业务数据）
    adt_chain_t* curr = adt_chain_begin->next;

    // 遍历链表 + 填充缓冲区
    while (curr != NULL && count < buf_size)
    {
        pbuf[count] = curr->data;  // 把data存入缓冲区
        count++;                   // 计数+1
        curr = curr->next;         // 下一个节点
    }

    osKernelUnlock();  // 解锁

    *code = SYSTEM_STATUS_OK;
    return count;  // 返回实际存入的节点数量
}

int get_chain_length(adt_chain_t* adt_chain_begin, int* code)
{
	// 入参校验
	if (code == NULL)
	{
		return 0;
	}

	if (adt_chain_begin == NULL)
	{
		*code = OPERATE_FAILED;
		return 0;
	}

	osKernelLock();  // 线程安全加锁

	int count = 0;
	// 从第一个有效数据节点开始遍历（跳过head）
	adt_chain_t* curr = adt_chain_begin->next;

	// 遍历计数
	while (curr != NULL)
	{
		count++;
		curr = curr->next;
	}

	osKernelUnlock();  // 解锁

	*code = SYSTEM_STATUS_OK;
	return count;  // 返回有效节点总数
}