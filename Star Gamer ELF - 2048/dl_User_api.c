#include "dl_extern_lib.h"

__attribute__((naked)) void *dl_vector_get_lib_func_entry(unsigned int index)
{
	__asm volatile(
		"svc 0x08\n"
		"bx lr\n"
	);
}
