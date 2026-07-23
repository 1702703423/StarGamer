#include "boot_crc.h"

static const u32 s_crc32_nibble_table[16] =
{
	0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
	0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
	0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
	0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

u32 Boot_Crc32Update(u32 crc, const u8 *data, u32 length)
{
	u32 i;

	for (i = 0U; i < length; i++)
	{
		crc ^= data[i];
		crc = s_crc32_nibble_table[crc & 0x0FU] ^ (crc >> 4U);
		crc = s_crc32_nibble_table[crc & 0x0FU] ^ (crc >> 4U);
	}

	return crc;
}

u32 Boot_Crc32Finish(u32 crc)
{
	return crc ^ BOOT_CRC32_XOR_OUT;
}

u32 Boot_Crc32(const u8 *data, u32 length)
{
	return Boot_Crc32Finish(Boot_Crc32Update(BOOT_CRC32_INITIAL, data, length));
}
