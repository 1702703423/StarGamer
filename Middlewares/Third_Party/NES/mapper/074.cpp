#include "nes_mapper.h" 


void MAP74_MMC3_set_CPU_banks(void);
void MAP74_MMC3_set_PPU_banks(void);
	
// Mapper 74
// much of this is based on the DarcNES source. thanks, nyef :)
void MAP74_Reset()
{ 
	int i;
  // clear registers FIRST!!!
  for( i = 0; i < 8; i++) MAPx->regs[i] = 0x00;
  // set CPU bank pointers
  MAPx->prg0 = 0;
  MAPx->prg1 = 1;
//  MAP74_MMC3_set_CPU_banks();

  // set VROM banks
  if(num_1k_VROM_banks)
  {
    MAPx->chr01 = 0;
    MAPx->chr23 = 2;
    MAPx->chr4  = 4;
    MAPx->chr5  = 5;
    MAPx->chr6  = 6;
    MAPx->chr7  = 7;
    MAP74_MMC3_set_PPU_banks();
  }
  else
  {
    MAPx->chr01 = MAPx->chr23 = MAPx->chr4 = MAPx->chr5 = MAPx->chr6 = MAPx->chr7 = 0;
  } 
  MAPx->irq_enabled = 0;
  MAPx->irq_counter = 0;
  MAPx->irq_latch = 0;
}

void MAP74_MemoryWrite(uint16 addr, uint8 data)
{
  switch(addr & 0xE001)
  {
    case 0x8000: 
    {
			MAPx->regs[0] = data;
			MAP74_MMC3_set_PPU_banks();
			MAP74_MMC3_set_CPU_banks();
    }
    break;

    case 0x8001:
      {
        uint32 bank_num;
        MAPx->regs[1] = data;
        bank_num = MAPx->regs[1];

				
        switch(MAPx->regs[0] & 0x07)
        {
          case 0x00:
          {
						if(num_1k_VROM_banks)
            {
              bank_num &= 0xfe;
							MAPx->chr01 = bank_num;
							MAP74_MMC3_set_PPU_banks();			
            }
          }
          break;

          case 0x01:
          {
            if(num_1k_VROM_banks)
            {
              bank_num &= 0xfe;
							MAPx->chr23 = bank_num;
							MAP74_MMC3_set_PPU_banks();			
            }
          }
          break;

          case 0x02:
          {
            if(num_1k_VROM_banks)
            {	
							MAPx->chr4 = bank_num;
							MAP74_MMC3_set_PPU_banks();
            }
					}
          break;

          case 0x03:
          {
            if(num_1k_VROM_banks)
            {
							MAPx->chr5 = bank_num;
							MAP74_MMC3_set_PPU_banks();
            }
          }
          break;

          case 0x04:
          {
            if(num_1k_VROM_banks)
            {
							MAPx->chr6 = bank_num;
							MAP74_MMC3_set_PPU_banks();			
            }
          }
          break;

          case 0x05:
          {
            if(num_1k_VROM_banks)
            {
							MAPx->chr7 = bank_num;
							MAP74_MMC3_set_PPU_banks();				
            }
          }
          break;

          case 0x06:
          {
						MAPx->prg0 = bank_num;
						MAP74_MMC3_set_CPU_banks();			 
          }
          break;

          case 0x07:
          {
						MAPx->prg1 = bank_num;
						MAP74_MMC3_set_CPU_banks();		  
          }
          break;
        }
      }
      break;

    case 0xA000:
      {
        MAPx->regs[2] = data;
				if ( !(RomHeader->flags_1&0x8 ))
        {
          if(data & 0x01)
          {
						set_mirroring(0,0,1,1);//Ë®Æ½¾µÏñ
          }
          else
          {
						set_mirroring(0,1,0,1);//´¹Ö±¾µÏñ
          }
        }
      }
      break;

    case 0xA001:
      {
        MAPx->regs[3] = data;
      }
      break;

    case 0xC000:
      MAPx->regs[4] = data;
			MAPx->irq_latch = data;
      break;

    case 0xC001:
      MAPx->regs[5] = data;
			MAPx->irq_latch = MAPx->regs[5];
      break;
		
    case 0xE000:
      MAPx->regs[6] = data;
      MAPx->irq_enabled = 0;
      break;

    case 0xE001:
      MAPx->regs[7] = data;
      MAPx->irq_enabled = 1;
      break;

    default:
      //LOG("MAPx: UNKNOWN: " << HEX(addr,4) << " = " << HEX(data) << endl);
      break;

  }
}

void MAP74_HSync(int scanline)
{
  if(MAPx->irq_enabled)
  {
    if((scanline >= 0) && (scanline <= 239)&&( spr_enabled() || bg_enabled()))
    {       
			if(!(MAPx->irq_counter--))
			{
				MAPx->irq_counter = MAPx->irq_latch;
				CPU_IRQ;
			}     
    }
  }
}

void MAP74_MMC3_set_CPU_banks()
{
  if(prg_swap())
  {
    set_CPU_banks(-2,MAPx->prg1,MAPx->prg0,-1);
  }
  else
  {
    set_CPU_banks(MAPx->prg0,MAPx->prg1,-2,-1);
  }
}

void MAP74_MMC3_set_PPU_banks()
{
  if(num_1k_VROM_banks)
  {
    if(chr_swap())
    {
      set_PPU_banks(MAPx->chr4,MAPx->chr5,MAPx->chr6,MAPx->chr7,MAPx->chr01,MAPx->chr01+1,MAPx->chr23,MAPx->chr23+1);
    }
    else
    {
      set_PPU_banks(MAPx->chr01,MAPx->chr01+1,MAPx->chr23,MAPx->chr23+1,MAPx->chr4,MAPx->chr5,MAPx->chr6,MAPx->chr7);
    }
  }
  else
  {
    if(chr_swap())
    {
      set_VRAM_bank(0, MAPx->chr4);
      set_VRAM_bank(1, MAPx->chr5);
      set_VRAM_bank(2, MAPx->chr6);
      set_VRAM_bank(3, MAPx->chr7);
      set_VRAM_bank(4, MAPx->chr01+0);
      set_VRAM_bank(5, MAPx->chr01+1);
      set_VRAM_bank(6, MAPx->chr23+0);
      set_VRAM_bank(7, MAPx->chr23+1);
    }
    else
    {
      set_VRAM_bank(0, MAPx->chr01+0);
      set_VRAM_bank(1, MAPx->chr01+1);
      set_VRAM_bank(2, MAPx->chr23+0);
      set_VRAM_bank(3, MAPx->chr23+1);
      set_VRAM_bank(4, MAPx->chr4);
      set_VRAM_bank(5, MAPx->chr5);
      set_VRAM_bank(6, MAPx->chr6);
      set_VRAM_bank(7, MAPx->chr7);
    }
  }
}
void MAP74_Init()
{
	NES_Mapper->Reset = MAP74_Reset;
	NES_Mapper->Write = MAP74_MemoryWrite; 
	NES_Mapper->HSync = MAP74_HSync;
}
