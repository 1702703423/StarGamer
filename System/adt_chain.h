#ifndef __ADT_CHAIN
#define __ADT_CHAIN

#include "System_Status.h"
#include "Malloc.h"

adt_chain_t* creat_chain(int memx,int* code);
adt_chain_t* insert_tail_chain(adt_chain_t* adt_chain_begin,void* data,int* code);
adt_chain_t* insert_head_chain(adt_chain_t* adt_chain_begin,void* data,int* code);
adt_chain_t* delete_node(adt_chain_t* adt_chain_begin, void* data,int* code);
int traverse_chain(adt_chain_t* adt_chain_begin, void** pbuf, int buf_size, int* code);
int get_chain_length(adt_chain_t* adt_chain_begin, int* code);

#endif