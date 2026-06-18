/*
*   @file dl_arch.c
*   @author wzh
*   @version: 1.0
*   @date  2021-12-3
*   @description 单片机程序动态加载的平台文件，主要实现重定位时，对机器码的更改
*   @brief	目前仅支持arm平台
*/
#include "dl_lib.h"
#include "dl_elf.h"
#include "dl_port.h"

#if defined(__arm__) && DL_USE_ARCH==DL_ARCH_ARM
//根据arm elf 文件规范编写
//部分参考了RT-Thread的Moudle实现
//arm elf 文件规范文档网址
//https://developer.arm.com/documentation/ihi0044/h/?lang=en#dynamic-linking
/*
 * Relocation type for arm
 */
#define R_ARM_NONE              0
#define R_ARM_PC24				1
#define R_ARM_ABS32             2
#define R_ARM_REL32             3
#define R_ARM_LDR_PC_G0			4
#define R_ARM_ABS16				5
#define R_ARM_ABS12				6
#define R_ARM_THM_ABS5			7
#define R_ARM_ABS8				8
#define R_ARM_SBREL32			9
#define R_ARM_THM_CALL          10
#define R_ARM_THM_PC8			11
#define R_ARM_BREL_ADJ			12
#define R_ARM_TLS_DESC			13
#define R_ARM_THM_SWI8			14
#define R_ARM_XPC25				15
#define R_ARM_THM_XPC22			16
#define R_ARM_TLS_DTPMOD32		17
#define R_ARM_TLS_DTPOFF32		18
#define R_ARM_TLS_TPOFF32		19
#define R_ARM_COPY				20
#define R_ARM_GLOB_DAT          21
#define R_ARM_JUMP_SLOT         22
#define R_ARM_RELATIVE          23
#define R_ARM_GOTOFF32			24
#define R_ARM_BASE_PREL			25
#define R_ARM_GOT_BREL          26
#define R_ARM_PLT32             27
#define R_ARM_CALL              28
#define R_ARM_JUMP24            29
#define R_ARM_THM_JUMP24        30
#define R_ARM_BASE_ABS			31
#define R_ARM_ALU_PCREL_7_0		32
#define R_ARM_ALU_PCREL_15_8	33
#define R_ARM_ALU_PCREL_23_15	34
#define R_ARM_LDR_SBREL_11_0_NC	35
#define R_ARM_ALU_SBREL_19_12_NC 36
#define R_ARM_ALU_SBREL_27_20_CK 37
#define R_ARM_TARGET1			38
#define R_ARM_SBREL31			39
#define R_ARM_V4BX              40
#define R_ARM_TARGET2			41
#define R_ARM_PREL31			42
#define R_ARM_MOVW_ABS_NC		43
#define R_ARM_MOVT_ABS			44
#define R_ARM_MOVW_PREL_NC		45
#define R_ARM_MOVT_PREL			46
#define R_ARM_THM_MOVW_ABS_NC	47
#define R_ARM_THM_MOVT_ABS		48
#define R_ARM_THM_MOVW_PREL_NC	49
#define R_ARM_THM_MOVT_PREL		50
#define R_ARM_THM_JUMP19		51
#define R_ARM_THM_JUMP6			52
#define R_ARM_THM_ALU_PREL_11_0 53

//实现根据特定指令，对机器码进行相应修改，每种芯片架构的方法都不同
//pHandle：句柄
//rel：重定位表，包含要更改的机器码的相对地址以及更改的方法描述
//sym_addr：符号地址，包含更改的内容
//返回值：DL_Err_Type 错误码
DL_Err_Type dl_relocate(DL_Handler *pHandle, Elf32_Rel *rel, Elf32_Addr sym_addr){
    Elf32_Addr *where;

    where = (Elf32_Addr *)((uint8_t *)pHandle->mem_ptr
                           + rel->r_offset
                           - pHandle->vstart_addr);
	//where 要更改的地址
	//sym_addr 符号的地址
    switch (ELF32_R_TYPE(rel->r_info))
    {
    case R_ARM_NONE:
        break;
    case R_ARM_ABS32:
        *where += (Elf32_Addr)sym_addr;
        DL_LOG_D("R_ARM_ABS32: %x -> %x",
                                       (uint32_t)where, *where);
        break;
    case R_ARM_PC24:
    case R_ARM_PLT32:
    case R_ARM_CALL:
    case R_ARM_JUMP24:{
		Elf32_Sword addend;
		Elf32_Addr tmp;
        addend = *where & 0x00ffffff;
        if (addend & 0x00800000)
            addend |= 0xff000000;
        tmp = sym_addr - (Elf32_Addr)where + (addend << 2);
        tmp >>= 2;
        *where = (*where & 0xff000000) | (tmp & 0x00ffffff);
        DL_LOG_D("R_ARM_PC24: %x -> %x",
                                       (uint32_t)where, *where);
	}
        break;
    case R_ARM_REL32:
        *where += sym_addr - (Elf32_Addr)where;
        DL_LOG_D("R_ARM_REL32: %x -> %x, sym %x, offset %x",
                      (uint32_t)where, *where, sym_addr, rel->r_offset);
        break;
    case R_ARM_V4BX:
        *where &= 0xf000000f;
        *where |= 0x01a0f000;
        break;

    case R_ARM_GLOB_DAT:
    case R_ARM_JUMP_SLOT:
        *where = (Elf32_Addr)sym_addr;
        DL_LOG_D("R_ARM_JUMP_SLOT: 0x%x -> 0x%x 0x%x\n",
                                       (uint32_t)where, *where, sym_addr);
        break;
#if 0        /* To do */
    case R_ARM_GOT_BREL:
        temp   = (Elf32_Addr)sym_val;
        *where = (Elf32_Addr)&temp;
        RT_DEBUG_LOG(RT_DEBUG_MODULE, ("R_ARM_GOT_BREL: 0x%x -> 0x%x 0x%x",
                                       where, *where, sym_val));
        break;
#endif

    case R_ARM_RELATIVE:
        *where = (Elf32_Addr)sym_addr + *where;
        DL_LOG_D("R_ARM_RELATIVE: 0x%x -> 0x%x 0x%x",
                                       (uint32_t)where, *where, sym_addr);
        break;
    case R_ARM_THM_CALL:
    case R_ARM_THM_JUMP24:{
		Elf32_Sword offset;
		uint32_t upper, lower, sign, j1, j2;
        upper  = *(uint16_t *)where;
        lower  = *(uint16_t *)((Elf32_Addr)where + 2);

        sign   = (upper >> 10) & 1;
        j1     = (lower >> 13) & 1;
        j2     = (lower >> 11) & 1;
        offset = (sign << 24) |
                 ((~(j1 ^ sign) & 1) << 23) |
                 ((~(j2 ^ sign) & 1) << 22) |
                 ((upper & 0x03ff) << 12) |
                 ((lower & 0x07ff) << 1);
        if (offset & 0x01000000)
            offset -= 0x02000000;
        offset += sym_addr - (Elf32_Addr)where;

        if (!(offset & 1) ||
            offset <= (int32_t)0xff000000 ||
            offset >= (int32_t)0x01000000)
        {
            DL_LOG_E("Module: Only Thumb addresses allowed");

            return DL_RELOCATE_ERR;
        }

        sign = (offset >> 24) & 1;
        j1   = sign ^ (~(offset >> 23) & 1);
        j2   = sign ^ (~(offset >> 22) & 1);
        *(uint16_t *)where = (uint16_t)((upper & 0xf800) |
                                              (sign << 10) |
                                              ((offset >> 12) & 0x03ff));
        *(uint16_t *)(where + 2) = (uint16_t)((lower & 0xd000) |
                                                    (j1 << 13) | (j2 << 11) |
                                                    ((offset >> 1) & 0x07ff));
        upper = *(uint16_t *)where;
        lower = *(uint16_t *)((Elf32_Addr)where + 2);
	}
        break;
	case R_ARM_THM_MOVW_ABS_NC:{
		uint32_t tmp;
		DL_LOG_D("MOVW_ABS_NC:sym_addr:0x%08x,*sym_addr:0x%08x",sym_addr,*(uint32_t*)sym_addr);
		tmp=*(uint32_t*)where;
		tmp=(tmp>>16)|(tmp<<16);//高低位互换
		tmp&=0xFBF08F00;//对原址[19:16],[26],[14:12],[7:0]清0
		sym_addr&=0x0000ffff;//高16位清0,MOVW指令仅支持16位立即数
		if(ELF_ST_TYPE(rel->r_info)==STT_FUNC) //确保代码跳转处于Thumb状态
			sym_addr|=1;
		tmp|=((sym_addr>>12)&0xf)<<16;//tmp[19:16]=(sym_addr>>12)&0xf
		tmp|=((sym_addr>>11)&0x1)<<26;//tmp[26]=(sym_addr>>11)&0x1
		tmp|=((sym_addr>>8 )&0x7)<<12;//tmp[14:12]=(sym_addr>>8 )&0x7;
		tmp|=sym_addr&0xff;//tmp[7:0]=sym_addr&0xff
		tmp=(tmp>>16)|(tmp<<16);//高低位互换
		DL_LOG_D("MOVW_ABS_NC:where:%p,*where:0x%08x,tmp:%08x",where,*where,tmp);
		*(uint32_t*)where=tmp;
	}
		break;
	case R_ARM_THM_MOVT_ABS:{
		uint32_t tmp;
		DL_LOG_D("R_ARM_THM_MOVT_ABS:sym_addr:0x%08x,*sym_addr:0x%08x",sym_addr,*(uint32_t*)sym_addr);
		tmp=*(uint32_t*)where;
		tmp=(tmp>>16)|(tmp<<16);//高低位互换
		tmp&=0xFBF08F00;//对原址[19:16],[26],[14:12],[7:0]清0
		sym_addr&=0xffff0000;//低16位清0,MOVT指令仅支持16位立即数
		tmp|=((sym_addr>>28)&0xf)<<16;//tmp[19:16]=(sym_addr>>28)&0xf
		tmp|=((sym_addr>>27)&0x1)<<26;//tmp[26]=(sym_addr>>17)&0x1
		tmp|=((sym_addr>>24)&0x7)<<12;//tmp[14:12]=(sym_addr>>24)&0x7;
		tmp|=(sym_addr>>16)&0xff;//tmp[7:0]=(sym_addr>>16)&0xff
		tmp=(tmp>>16)|(tmp<<16);//高低位互换
		DL_LOG_D("MOVT_ABS:where:%p,*where:0x%08x,tmp:%08x",where,*where,tmp);
		*(uint32_t*)where=tmp;
	}
		break;
    default:
        return DL_RELOCATE_ERR;
    }
    return DL_NO_ERR;
}
#endif



