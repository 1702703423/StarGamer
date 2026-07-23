#ifndef BOOT_CRC_H
#define BOOT_CRC_H

#include "boot_platform.h"

#define BOOT_CRC32_INITIAL       0xFFFFFFFFUL
#define BOOT_CRC32_XOR_OUT       0xFFFFFFFFUL

u32 Boot_Crc32Update(u32 crc, const u8 *data, u32 length);
u32 Boot_Crc32Finish(u32 crc);
u32 Boot_Crc32(const u8 *data, u32 length);

#endif
