#ifndef MEMORY_DUMP_H
#define MEMORY_DUMP_H

#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEMORY_DUMP_FLASH_ADDRESS       0x00200000UL
#define MEMORY_DUMP_FLASH_SIZE          0x00400000UL
#define MEMORY_DUMP_EXPORT_PATH         "0:/hardfault_dump.bin"

typedef struct Memory_Dump_FaultRecord
{
	u32 stack_pointer;
	u32 frame_pointer;
	u32 exc_return;
	u32 msp;
	u32 psp;
	u32 cfsr;
	u32 hfsr;
	u32 dfsr;
	u32 afsr;
	u32 mmfar;
	u32 bfar;
	u32 r0;
	u32 r1;
	u32 r2;
	u32 r3;
	u32 r12;
	u32 stacked_lr;
	u32 pc;
	u32 xpsr;
	u8 frame_valid;
	u8 fp_extended_frame;
} Memory_Dump_FaultRecord_t;

typedef enum Memory_Dump_Result
{
	MEMORY_DUMP_RESULT_OK = 0,
	MEMORY_DUMP_RESULT_DISABLED,
	MEMORY_DUMP_RESULT_NONE,
	MEMORY_DUMP_RESULT_ALREADY_EXPORTED,
	MEMORY_DUMP_RESULT_FLASH_ERROR,
	MEMORY_DUMP_RESULT_INVALID,
	MEMORY_DUMP_RESULT_NO_MEMORY,
	MEMORY_DUMP_RESULT_FATFS_UNAVAILABLE,
	MEMORY_DUMP_RESULT_FILE_ERROR,
	MEMORY_DUMP_RESULT_CHECKSUM_ERROR
} Memory_Dump_Result_t;

typedef void (*Memory_Dump_ProgressCallback_t)(u8 percent, const char* phase);

Memory_Dump_Result_t Memory_Dump_Capture(
	const Memory_Dump_FaultRecord_t* fault,
	Memory_Dump_ProgressCallback_t progress);
Memory_Dump_Result_t Memory_Dump_ExportPending(void);
const char* Memory_Dump_ResultText(Memory_Dump_Result_t result);

#ifdef __cplusplus
}
#endif

#endif
