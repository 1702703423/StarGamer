#include "nes_main.h" 
#include "nes_ppu.h"
#include "nes_mapper.h"
#include "nes_apu.h"

#include "string.h"
#include "fatfs.h"
#include "Malloc.h"
#include "i2s.h"
#include "key.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序移植自网友ye781205的NES模拟器工程
//ALIENTEK STM32F407开发板
//NES主函数 代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2014/7/1
//版本：V1.0  			  
////////////////////////////////////////////////////////////////////////////////// 	 

#define ROM_SIZE_MAX (768 * 1024) 

u8 nes_frame_cnt;		//nes帧计数器 
int MapperNo;			//map编号
int NES_scanline;		//nes扫描线
int VROM_1K_SIZE;
int VROM_8K_SIZE;
u32 NESrom_crc32;

volatile u32 PADdata0;  /* joypad1 [7:0]: R L D U Start Select B A */
volatile u32 PADdata1;  /* joypad2 [7:0]: R L D U Start Select B A */
u8 *NES_RAM;			//保持1024字节对齐
u8 *NES_SRAM;  
NES_header *RomHeader; 	//rom文件头
MAPPER *NES_Mapper;		 
MapperCommRes *MAPx; 


u8* spr_ram;			//精灵RAM,256字节
ppu_data* ppu;			//ppu指针
u8* VROM_banks;
u8* VROM_tiles;

apu_t *apu; 			//apu指针
u8 *wave_buffers; 

u8* romfile;			//nes文件指针,指向整个nes文件的起始地址.
//////////////////////////////////////////////////////////////////////////////////////

u8 nes_xoff=0;	//显示在x轴方向的偏移量(实际显示宽度=256-2*nes_xoff)

#define NES_APU_SAMPLE_BITS        8
#define NES_APU_CHANNELS           1
#define NES_APU_VOLUME             50
#define NES_APU_PCM_SILENCE        0x80
#define NES_APU_DMA_WAIT_TICKS     25

#define NES_PAD_A                  0x01U
#define NES_PAD_B                  0x02U
#define NES_PAD_SELECT             0x04U
#define NES_PAD_START              0x08U
#define NES_PAD_UP                 0x10U
#define NES_PAD_DOWN               0x20U
#define NES_PAD_LEFT               0x40U
#define NES_PAD_RIGHT              0x80U

static volatile u8 nes_apu_dma_ready_mask = 0;
static volatile u8 nes_apu_dma_running = 0;
static u16 nes_apu_dma_pkg_size = APU_PCMBUF_SIZE;

static int nes_apu_try_take_ready_buffer(void)
{
	u32 primask;
	u8 mask;
	u8 idle_index;
	u8 idle_mask;
	int dma_index = -1;

	primask = __get_PRIMASK();
	__disable_irq();

	mask = nes_apu_dma_ready_mask;
	idle_index = audio_card_get_idle_buffer_index();
	idle_mask = (u8)(1U << idle_index);
	if(mask & idle_mask)
	{
		nes_apu_dma_ready_mask = ((mask & 0x03) == 0x03) ? 0 : (mask & (u8)~idle_mask);
		dma_index = idle_index;
	}
	else if(mask != 0)
	{
		nes_apu_dma_ready_mask = 0;
	}

	__set_PRIMASK(primask);

	return dma_index;
}

static void nes_apu_wait_tick(void)
{
	if(osKernelGetState() == osKernelRunning)
	{
		osDelay(1);
	}
}

static int nes_apu_take_ready_buffer(void)
{
	u8 wait_ticks = NES_APU_DMA_WAIT_TICKS;
	int dma_index;

	while(wait_ticks--)
	{
		dma_index = nes_apu_try_take_ready_buffer();
		if(dma_index >= 0)
		{
			return dma_index;
		}

		nes_apu_wait_tick();
	}

	return -1;
}

static void nes_apu_make_silence(u8 *buffer, u16 samples)
{
	if(buffer != NULL)
	{
		memset(buffer, NES_APU_PCM_SILENCE, samples);
	}
}

#if 1
//加载ROM
//返回值:0,成功
//    1,内存错误
//    3,map错误

u8 nes_load_rom(void)
{  
	u8* p;  
	u8 i;
	u8 res=0;
	p=(u8*)romfile;	
	if(strncmp((char*)p,"NES",3)==0)
	{  
		RomHeader->ctrl_z=p[3];
		RomHeader->num_16k_rom_banks=p[4];
		RomHeader->num_8k_vrom_banks=p[5];
		RomHeader->flags_1=p[6];
		RomHeader->flags_2=p[7]; 
		if(RomHeader->flags_1&0x04)p+=512;		//有512字节的trainer:
		if(RomHeader->num_8k_vrom_banks>0)		//存在VROM,进行预解码
		{		
			VROM_banks=p+16+(RomHeader->num_16k_rom_banks*0x4000);
#if	NES_RAM_SPEED==1	//1:内存占用小 0:速度快	 
			VROM_tiles=VROM_banks;	 
#else  
			VROM_tiles=sys_malloc(SRAMEX,RomHeader->num_8k_vrom_banks*8*1024);//这里可能申请多达1MB内存!!!
			if(VROM_tiles==0)VROM_tiles=VROM_banks;//内存不够用的情况下,尝试VROM_titles与VROM_banks共用内存			
			compile(RomHeader->num_8k_vrom_banks*8*1024/16,VROM_banks,VROM_tiles);  
#endif	
		}else 
		{
			VROM_banks = sys_malloc(1,8*1024);
			VROM_tiles = sys_malloc(1,8*1024);
			if(!VROM_banks||!VROM_tiles)res=1;
		}  	
		VROM_1K_SIZE = RomHeader->num_8k_vrom_banks * 8;
		VROM_8K_SIZE = RomHeader->num_8k_vrom_banks;  
		MapperNo=(RomHeader->flags_1>>4)|(RomHeader->flags_2&0xf0);
		if(RomHeader->flags_2 & 0x0E)MapperNo=RomHeader->flags_1>>4;//忽略高四位，如果头看起来很糟糕 
//		Debug("use map:%d\r\n",MapperNo);
		for(i=0;i<255;i++)  // 查找支持的Mapper号
		{		
			if (MapTab[i]==MapperNo)break;		
			if (MapTab[i]==-1)res=3; 
		} 
		if(res==0)
		{
			switch(MapperNo)
			{
				case 1:  
					MAP1=sys_malloc(1,sizeof(Mapper1Res)); 
					if(!MAP1)res=1;
					break;
				case 4:  
				case 6: 
				case 16:
				case 17:
				case 18:
				case 19:
				case 21: 
				case 23:
				case 24:
				case 25:
				case 64:
				case 65:
				case 67:
				case 69:
				case 85:
				case 189:
					MAPx=sys_malloc(1,sizeof(MapperCommRes)); 
					if(!MAPx)res=1;
					break;  
				default:
					break;
			}
		}
	} 
	return res;	//返回执行结果
} 
//释放内存 
void nes_sram_free(void)
{ 
	sys_free(1,NES_RAM);		
	sys_free(1,NES_SRAM);	
	sys_free(1,RomHeader);	
	sys_free(1,NES_Mapper);
	sys_free(1,spr_ram);		
	sys_free(1,ppu);	
	sys_free(1,apu);	
	sys_free(1,wave_buffers);	
	sys_free(1,romfile);
	if((VROM_tiles!=VROM_banks)&&VROM_banks&&VROM_tiles)//如果分别为VROM_banks和VROM_tiles申请了内存,则释放
	{
		sys_free(1,VROM_banks);
		sys_free(1,VROM_tiles);	
	}
	switch (MapperNo)//释放map内存
	{
		case 1: 			//释放内存
			sys_free(1,MAP1);
			break;	 	
		case 4: 
		case 6: 
		case 16:
		case 17:
		case 18:
		case 19:
		case 21:
		case 23:
		case 24:
		case 25:
		case 64:
		case 65:
		case 67:
		case 69:
		case 85:
		case 189:
			sys_free(1,MAPx);break;	 		//释放内存 
		default:break; 
	}
	NES_RAM=0;	
	NES_SRAM=0;
	RomHeader=0;
	NES_Mapper=0;
	spr_ram=0;
	ppu=0;
	apu=0;
	wave_buffers=0;
	romfile=0; 
	VROM_banks=0;
	VROM_tiles=0; 
	MAP1=0;
	MAPx=0;
} 
//为NES运行申请内存
//romsize:nes文件大小
//返回值:0,申请成功
//       1,申请失败
u8 nes_sram_malloc(u32 romsize)
{
	u16 i=0;
	for(i=0;i<64;i++)//为NES_RAM,查找1024对齐的内存
	{
		NES_SRAM=sys_malloc(1,i*32);
		NES_RAM=sys_malloc(1,0X800);	//申请2K字节,必须1024字节对齐
		if((u32)NES_RAM%1024)			//不是1024字节对齐
		{
			sys_free(1,NES_RAM);		//释放内存,然后重新尝试分配
			sys_free(1,NES_SRAM); 
		}else 
		{
			sys_free(1,NES_SRAM); 	//释放内存
			break;
		}
	}
 	NES_SRAM=sys_malloc(1,0X2000);
	RomHeader=sys_malloc(1,sizeof(NES_header));
	NES_Mapper=sys_malloc(1,sizeof(MAPPER));
	spr_ram=sys_malloc(1,0X100);		
	ppu=sys_malloc(1,sizeof(ppu_data));  
	apu=sys_malloc(1,sizeof(apu_t));		//sizeof(apu_t)=  12588
	wave_buffers=sys_malloc(1,APU_PCMBUF_SIZE);
	
	if(romsize > ROM_SIZE_MAX) //最大限制
	{
		return 1;
	}
	
 	romfile=sys_malloc(SRAMEX,romsize);		//申请游戏rom空间,固定大小，NES_ROM_MEM_SIZE
	
	if(i==64||!NES_RAM||!NES_SRAM||!RomHeader||!NES_Mapper||!spr_ram||!ppu||!apu||!wave_buffers||!romfile)
	{
		nes_sram_free();
		return 1;
	}
	memset(NES_SRAM,0,0X2000);				//清零
	memset(RomHeader,0,sizeof(NES_header));	//清零
	memset(NES_Mapper,0,sizeof(MAPPER));	//清零
	memset(spr_ram,0,0X100);				//清零
	memset(ppu,0,sizeof(ppu_data));			//清零
	memset(apu,0,sizeof(apu_t));			//清零
	memset(wave_buffers,0,APU_PCMBUF_SIZE);//清零
	memset(romfile,0,romsize);				//清零 
	return 0;
} 
//开始nes游戏
//pname:nes游戏路径
//返回值:
//0,正常退出
//1,内存错误
//2,文件错误
//3,不支持的map
u8 nes_load(u8* pname)
{
	FIL *file;
	FILINFO FileInf;
	UINT br;
	u8 res=0;
	
	file = sys_malloc(1,sizeof(FIL));  
	if(file==0)return 1;						//内存申请失败.  
	res=f_open(file,(char*)pname,FA_READ);
	if(res!=FR_OK)	//打开文件失败
	{
		sys_free(1,file);
		return 2;
	}
	
	f_stat((char*)pname, &FileInf);
	res=nes_sram_malloc(FileInf.fsize);			//申请内存 
	if(res==0)
	{
		f_read(file,romfile,FileInf.fsize,&br);	//读取nes文件
		NESrom_crc32=get_crc32(romfile+16, FileInf.fsize-16);//获取CRC32的值	
		res=nes_load_rom();						//加载ROM
		if(res==0) 					
		{   
			cpu6502_init();						//初始化6502,并复位	 
			Mapper_Init();						//map初始化 	 
			PPU_reset();						//ppu复位
			apu_init(); 						//apu初始化 
			nes_sound_open(0,APU_SAMPLE_RATE);	//初始化播放设备
			nes_emulate_frame();				//进入NES模拟器主循环 
			nes_sound_close();					//关闭声音输出
			ppu_close(); //释放内存
		}
	}
	f_close(file);
	sys_free(1,file);//释放内存
	nes_sram_free();	//释放内存
	return res;
}  

//设置游戏显示窗口
void nes_set_window(void)
{	
  nes_xoff = 0;
	LCD_SetWindows(1,0,272,239);
}

//读取游戏手柄数据
void nes_get_gamepadval(void)
{ 
	u32 key_state;
	u32 pad = 0;

	key_state = KEY_GetState();

	if((key_state & KEY_NES_UP) != 0U) pad |= NES_PAD_UP;
	if((key_state & KEY_NES_DOWN) != 0U) pad |= NES_PAD_DOWN;
	if((key_state & KEY_NES_LEFT) != 0U) pad |= NES_PAD_LEFT;
	if((key_state & KEY_NES_RIGHT) != 0U) pad |= NES_PAD_RIGHT;
	if((key_state & KEY_NES_START) != 0U) pad |= NES_PAD_START;
	if((key_state & KEY_NES_A) != 0U) pad |= NES_PAD_A;
	if((key_state & KEY_NES_B) != 0U) pad |= NES_PAD_B;

	PADdata0 = pad;
	PADdata1 = 0;
}

static u8 nes_exit_requested(void)
{
	return ((KEY_GetState() & KEY_NES_EXIT) != 0U) ? 1U : 0U;
}

//nes模拟器主循环
void nes_emulate_frame()
{  
	u8 nes_frame = 0;
	nes_set_window();//设置窗口
//	Debug("NES Run\n");
//	LCD_Clear(BLACK);
	for(;;)
	{	
		if(nes_exit_requested()) break;
		if(nes_frame==0)
		{
			nes_set_window();
		}
		// LINES 0-239
		PPU_start_frame();
		for(NES_scanline = 0; NES_scanline< 240; NES_scanline++)
		{
			run6502(113*256);
			NES_Mapper->HSync(NES_scanline);
			//扫描一行		  
			if(nes_frame==0)
				scanline_draw(NES_scanline);
			else 
				do_scanline_and_dont_draw(NES_scanline); 
		}  
		NES_scanline=240;
		run6502(113*256);//运行1线
		NES_Mapper->HSync(NES_scanline); 
		start_vblank(); 
		if(NMI_enabled()) 
		{
			cpunmi=1;
			run6502(7*256);//运行中断
		}
		NES_Mapper->VSync();
		// LINES 242-261    
		for(NES_scanline=241;NES_scanline<262;NES_scanline++)
		{
			run6502(113*256);	  
			NES_Mapper->HSync(NES_scanline);		  
		}	   
		end_vblank(); 
		nes_get_gamepadval();//每3帧查询一次USB
		apu_soundoutput();//输出游戏声音	 
		nes_frame_cnt++; 	
		nes_frame++;
		if(nes_frame>NES_SKIP_FRAME)nes_frame=0;//跳帧
		osThreadYield();
	}

}

////////////////////////////////////////////////////////////////////////////////// 	 
//NES打开音频输出
int nes_sound_open(int samples_per_sync,int sample_rate) 
{	
	u16 pkg_size;

	if(wave_buffers == NULL)
	{
		return 0;
	}

	if(sample_rate <= 0)
	{
		sample_rate = APU_SAMPLE_RATE;
	}

	pkg_size = (samples_per_sync > 0) ? (u16)samples_per_sync : (u16)(sample_rate / 60);
	if((pkg_size == 0) || (pkg_size > APU_PCMBUF_SIZE))
	{
		pkg_size = APU_PCMBUF_SIZE;
	}

	nes_apu_dma_running = 0;
	nes_apu_dma_ready_mask = 0;
	nes_apu_dma_pkg_size = pkg_size;

	audio_card_init(NES_APU_VOLUME);
	if(audio_card_open_prepare(sample_rate, NES_APU_SAMPLE_BITS, NES_APU_CHANNELS, pkg_size) != 0)
	{
		return 0;
	}

	audio_card_register_tx(nes_i2s_dma_tx_callback);
	nes_apu_make_silence(wave_buffers, pkg_size);
	audio_card_write_to_buffer(0, wave_buffers, pkg_size);
	audio_card_write_to_buffer(1, wave_buffers, pkg_size);

	if(audio_card_start() != 0)
	{
		audio_card_close();
		return 0;
	}

	nes_apu_dma_running = 1;
	return 1;
}
//NES关闭音频输出
void nes_sound_close(void) 
{ 
	nes_apu_dma_running = 0;
	nes_apu_dma_ready_mask = 0;
	audio_card_register_tx(NULL);
	audio_card_close();

}

#endif

//在6502.s里面被调用
void debug_6502(u16 reg0,u8 reg1)
{
//	shellPrintEndLine("6502 error:%x,%d\r\n",reg0,reg1);
}

void nes_apu_fill_buffer(int samples,u8* wavebuf)
{	
	int dma_index;
	u16 pkg_size;

	if((!nes_apu_dma_running) || (wavebuf == NULL))
	{
		return;
	}

	pkg_size = (samples > 0) ? (u16)samples : nes_apu_dma_pkg_size;
	if(pkg_size > nes_apu_dma_pkg_size)
	{
		pkg_size = nes_apu_dma_pkg_size;
	}

	dma_index = nes_apu_take_ready_buffer();
	if(dma_index < 0)
	{
		return;
	}

	audio_card_write_to_buffer((u8)dma_index, wavebuf, pkg_size);
}

void nes_i2s_dma_tx_callback(void)
{
	if(!nes_apu_dma_running)
	{
		return;
	}

	nes_apu_dma_ready_mask |= (u8)(1U << audio_card_get_idle_buffer_index());
}
