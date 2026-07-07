#include "adt_chain.h"
#include "System_Status.h"

adt_chain_t* creat_chain(int memx, int* code)
{
	if (code == NULL)
	{
		return NULL;
	}

	osKernelLock();

	adt_chain_t* adt_chain_head = (adt_chain_t*)sys_malloc(memx, sizeof(adt_chain_t));
	if (adt_chain_head == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}

	adt_chain_head->prev = NULL;
	adt_chain_head->next = NULL;
	adt_chain_head->data = NULL;
	adt_chain_head->memx = memx;

	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return adt_chain_head;
}

adt_chain_t* insert_tail_chain(adt_chain_t* adt_chain_begin, void* data, int* code)
{
	if (code == NULL)
	{
		return NULL;
	}

	if (adt_chain_begin == NULL || data == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}

	osKernelLock();

	adt_chain_t* pointer = adt_chain_begin;
	adt_chain_t* temp = (adt_chain_t*)sys_malloc(adt_chain_begin->memx, sizeof(adt_chain_t));
	if (temp == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}

	temp->prev = NULL;
	temp->next = NULL;
	temp->data = data;
	temp->memx = adt_chain_begin->memx;

	/* 遍历到尾部 */
	while (pointer->next != NULL)
	{
		pointer = pointer->next;
	}

	/* 尾节点 <-> 新节点 */
	pointer->next = temp;
	temp->prev = pointer;

	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return adt_chain_begin;
}

adt_chain_t* insert_head_chain(adt_chain_t* adt_chain_begin, void* data, int* code)
{
	if (adt_chain_begin == NULL || data == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}

	osKernelLock();

	adt_chain_t* temp = (adt_chain_t*)sys_malloc(adt_chain_begin->memx, sizeof(adt_chain_t));
	if (temp == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}

	temp->prev = NULL;
	temp->next = NULL;
	temp->data = data;
	temp->memx = adt_chain_begin->memx;

	/* 头节点 <-> 新节点 <-> 旧首节点 */
	temp->prev = adt_chain_begin;
	temp->next = adt_chain_begin->next;

	if (adt_chain_begin->next != NULL)
	{
		adt_chain_begin->next->prev = temp;
	}

	adt_chain_begin->next = temp;

	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return adt_chain_begin;
}

adt_chain_t* delete_node(adt_chain_t* adt_chain_begin, void* data, int* code)
{
	if (code == NULL)
	{
		return NULL;
	}

	if (adt_chain_begin == NULL || data == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}

	osKernelLock();

	adt_chain_t* curr = adt_chain_begin->next;

	while (curr != NULL)
	{
		if (curr->data == data)
		{
			/* 从链表中摘除 curr */
			curr->prev->next = curr->next;

			if (curr->next != NULL)
			{
				curr->next->prev = curr->prev;
			}

			sys_free(curr->memx, curr);
			osKernelUnlock();
			*code = SYSTEM_STATUS_OK;
			return adt_chain_begin;
		}

		curr = curr->next;
	}

	osKernelUnlock();

	*code = OPERATE_FAILED;
	return adt_chain_begin;
}

adt_chain_t* destroy_chain(adt_chain_t* adt_chain_begin, int* code)
{
	if (code == NULL)
	{
		return NULL;
	}

	if (adt_chain_begin == NULL)
	{
		*code = OPERATE_FAILED;
		return NULL;
	}

	osKernelLock();

	adt_chain_t* pointer = adt_chain_begin;
	adt_chain_t* temp = NULL;

	while (pointer != NULL)
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
	/* 入参合法性检查 */
	if (code == NULL)
	{
		return 0;
	}

	if (adt_chain_begin == NULL || pbuf == NULL || buf_size <= 0)
	{
		*code = OPERATE_FAILED;
		return 0;
	}

	osKernelLock();

	int count = 0;
	/* 跳过头节点，从头节点的后继开始正向遍历 */
	adt_chain_t* curr = adt_chain_begin->next;

	while (curr != NULL && count < buf_size)
	{
		pbuf[count] = curr->data;
		count++;
		curr = curr->next;
	}

	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return count;
}

int r_traverse_chain(adt_chain_t* adt_chain_begin, void** pbuf, int buf_size, int* code)
{
	/* 入参合法性检查 */
	if (code == NULL)
	{
		return 0;
	}

	if (adt_chain_begin == NULL || pbuf == NULL || buf_size <= 0)
	{
		*code = OPERATE_FAILED;
		return 0;
	}

	osKernelLock();

	/* 先跳到尾部 */
	adt_chain_t* curr = adt_chain_begin;
	while (curr->next != NULL)
	{
		curr = curr->next;
	}

	int count = 0;
	/* 从尾节点反向遍历到头节点的后继（不含头节点） */
	while (curr != adt_chain_begin && count < buf_size)
	{
		pbuf[count] = curr->data;
		count++;
		curr = curr->prev;
	}

	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return count;
}

int get_chain_length(adt_chain_t* adt_chain_begin, int* code)
{
	/* 入参校验 */
	if (code == NULL)
	{
		return 0;
	}

	if (adt_chain_begin == NULL)
	{
		*code = OPERATE_FAILED;
		return 0;
	}

	osKernelLock();

	int count = 0;
	/* 从第一个有效数据节点开始遍历（跳过 head） */
	adt_chain_t* curr = adt_chain_begin->next;

	while (curr != NULL)
	{
		count++;
		curr = curr->next;
	}

	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return count;
}
