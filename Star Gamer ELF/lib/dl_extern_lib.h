#ifndef DL_EXTERN_LIB_H__
#define DL_EXTERN_LIB_H__

#include <string.h>

void* dl_vector_get_lib_func_entry(unsigned int index);

#define DL_STATIC_ASSERT(exp)	typedef int __ai[(exp)?1:-1]
  
#define DL_FUNC_NAME(name)		__dl_use_##name
#define DL_DECLARE_EXTERN_LIB_FUNC(func,base,max_func_num,index,ret,...) \
                                    static ret (*DL_FUNC_NAME(func))(__VA_ARGS__)=0;\
                                    do{\
                                    if(DL_FUNC_NAME(func)==0)\
                                    {DL_FUNC_NAME(func)=(ret (*)(__VA_ARGS__))dl_vector_get_lib_func_entry(index+base);}\
                                    }while(0);\
                                    DL_STATIC_ASSERT(index<max_func_num)

#define DL_STRING_LIB_BASE		(0)
#define DL_STRING_LIB_NUM		(22)

#endif
