#ifndef __ADT_CHAIN
#define __ADT_CHAIN

#include "sys.h"
#include "Malloc.h"

/* 双向链表节点 */
typedef struct adt_chain
{
	uint8_t memx;               /* 内存池类型 (SRAMCCM/SRAMEX) */
	struct adt_chain* prev;     /* 前驱节点 */
	struct adt_chain* next;     /* 后继节点 */
	void* data;                 /* 节点数据 */
} adt_chain_t;

/* ---- 链表操作 API ---- */
adt_chain_t* creat_chain(int memx, int* code);
adt_chain_t* insert_tail_chain(adt_chain_t* adt_chain_begin, void* data, int* code);
adt_chain_t* insert_head_chain(adt_chain_t* adt_chain_begin, void* data, int* code);
adt_chain_t* delete_node(adt_chain_t* adt_chain_begin, void* data, int* code);
adt_chain_t* destroy_chain(adt_chain_t* adt_chain_begin, int* code);
int traverse_chain(adt_chain_t* adt_chain_begin, void** pbuf, int buf_size, int* code);
int r_traverse_chain(adt_chain_t* adt_chain_begin, void** pbuf, int buf_size, int* code);
int get_chain_length(adt_chain_t* adt_chain_begin, int* code);

#endif
