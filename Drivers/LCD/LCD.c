#include "LCD.h"
#include "LCD_Font.h"

SPI_HandleTypeDef *lcd_spi;
static int LCD_WaitForSPI(void);

//管理LCD重要参数
//默认为竖屏
_lcd_dev lcddev;

//画笔颜色,背景颜色
u16 POINT_COLOR = 0x0000,BACK_COLOR = 0xFFFF;  
u16 DeviceCode;

u8 lcd_data[2]={0};

static void Ui16ToUin8(u16 adata)
{
	lcd_data[0] = (adata >>8) & 0xFF; //前面(uint8_t)为强制类型转换
	lcd_data[1] = (adata & 0xFF);
}

/*****************************************************************************
 * @name       :void LCD_WR_REG(u8 data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit command to the LCD screen
 * @parameters :data:Command value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_REG(u8 data)
{ 
	LCD_CS_CLR;     
	LCD_RS_CLR;	  
	LCD_WRITE_BYTE(&data, 1);
	LCD_CS_SET;	
}

/*****************************************************************************
 * @name       :void LCD_WR_DATA(u8 data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit data to the LCD screen
 * @parameters :data:data value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_DATA(u8 data)
{
   LCD_CS_CLR;
	 LCD_RS_SET;
   LCD_WRITE_BYTE(&data, 1);
   LCD_CS_SET;
}

/*****************************************************************************
 * @name       :void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue)
 * @date       :2018-08-09 
 * @function   :Write data into registers
 * @parameters :LCD_Reg:Register address
                LCD_RegValue:Data to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue)
{	
	LCD_WR_REG(LCD_Reg);  
	LCD_WR_DATA(LCD_RegValue);	    		 
}	   

/*****************************************************************************
 * @name       :void LCD_WriteRAM_Prepare(void)
 * @date       :2018-08-09 
 * @function   :Write GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 
void LCD_WriteRAM_Prepare(void)
{
	LCD_WR_REG(lcddev.wramcmd);
}	 

/*****************************************************************************
 * @name       :void Lcd_WriteData_16Bit(u16 Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :Data:Data to be written
 * @retvalue   :None
******************************************************************************/	 
void Lcd_WriteData_16Bit(u16 Data)
{	
	LCD_CS_CLR;
	LCD_RS_SET;  
	Ui16ToUin8(Data);
	LCD_WRITE_BYTE(lcd_data, 2);
	LCD_CS_SET;
}

/*****************************************************************************
 * @name       :void LCD_DrawPoint(u16 x,u16 y)
 * @date       :2018-08-09 
 * @function   :Write a pixel data at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/	
void LCD_DrawPoint(u16 x,u16 y)
{
	LCD_SetCursor(x,y);//设置光标位置 
	Lcd_WriteData_16Bit(POINT_COLOR); 
}

/*****************************************************************************
 * @name       :void LCD_Clear(u16 Color)
 * @date       :2018-08-09 
 * @function   :Full screen filled LCD screen
 * @parameters :color:Filled color
 * @retvalue   :None
******************************************************************************/	
void LCD_Clear(u16 Color)
{
  unsigned int i,m;  
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);   
	LCD_CS_CLR;
	LCD_RS_SET;
	for(i=0;i<lcddev.height;i++)
	{
    for(m=0;m<lcddev.width;m++)
    {	
			Ui16ToUin8(Color);
			LCD_WRITE_BYTE(lcd_data, 2);
		}
	}
  LCD_CS_SET;
} 

/*****************************************************************************
 * @name       :void LCD_RESET(void)
 * @date       :2018-08-09 
 * @function   :Reset LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_RESET(void)
{
	LCD_RST_CLR;
	HAL_Delay(120);	
	LCD_RST_SET;
	HAL_Delay(120);
}

/*****************************************************************************
 * @name       :void LCD_RESET(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 	 
void LCD_Init(SPI_HandleTypeDef *spi)
{ 									 
	lcd_spi = spi;
 	LCD_RESET(); //LCD 复位
//************* ST7789初始化**********//	
	LCD_WR_REG(0x36); 
	LCD_WR_DATA(0x00);

	LCD_WR_REG(0x3A); 
	LCD_WR_DATA(0x05);

	LCD_WR_REG(0xB2);
	LCD_WR_DATA(0x0C);
	LCD_WR_DATA(0x0C);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x33);
	LCD_WR_DATA(0x33);

	LCD_WR_REG(0xB7); 
	LCD_WR_DATA(0x35);  

	LCD_WR_REG(0xBB);
	LCD_WR_DATA(0x19);

	LCD_WR_REG(0xC0);
	LCD_WR_DATA(0x2C);

	LCD_WR_REG(0xC2);
	LCD_WR_DATA(0x01);

	LCD_WR_REG(0xC3);
	LCD_WR_DATA(0x12);   

	LCD_WR_REG(0xC4);
	LCD_WR_DATA(0x20);  

	LCD_WR_REG(0xC6); 
	LCD_WR_DATA(0x0F);    

	LCD_WR_REG(0xD0); 
	LCD_WR_DATA(0xA4);
	LCD_WR_DATA(0xA1);

	LCD_WR_REG(0xE0);
	LCD_WR_DATA(0xD0);
	LCD_WR_DATA(0x04);
	LCD_WR_DATA(0x0D);
	LCD_WR_DATA(0x11);
	LCD_WR_DATA(0x13);
	LCD_WR_DATA(0x2B);
	LCD_WR_DATA(0x3F);
	LCD_WR_DATA(0x54);
	LCD_WR_DATA(0x4C);
	LCD_WR_DATA(0x18);
	LCD_WR_DATA(0x0D);
	LCD_WR_DATA(0x0B);
	LCD_WR_DATA(0x1F);
	LCD_WR_DATA(0x23);

	LCD_WR_REG(0xE1);
	LCD_WR_DATA(0xD0);
	LCD_WR_DATA(0x04);
	LCD_WR_DATA(0x0C);
	LCD_WR_DATA(0x11);
	LCD_WR_DATA(0x13);
	LCD_WR_DATA(0x2C);
	LCD_WR_DATA(0x3F);
	LCD_WR_DATA(0x44);
	LCD_WR_DATA(0x51);
	LCD_WR_DATA(0x2F);
	LCD_WR_DATA(0x1F);
	LCD_WR_DATA(0x1F);
	LCD_WR_DATA(0x20);
	LCD_WR_DATA(0x23);

	LCD_WR_REG(0x21); 

	LCD_WR_REG(0x11); 
	//Delay (120); 

	LCD_WR_REG(0x29); 	
  LCD_direction(USE_HORIZONTAL);//设置LCD显示方向
//	LCD_LED=1;//点亮背光	 
}
 
/*****************************************************************************
 * @name       :void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
 * @date       :2018-08-09 
 * @function   :Setting LCD display window
 * @parameters :xStar:the bebinning x coordinate of the LCD display window
								yStar:the bebinning y coordinate of the LCD display window
								xEnd:the endning x coordinate of the LCD display window
								yEnd:the endning y coordinate of the LCD display window
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
{	
	LCD_WR_REG(lcddev.setxcmd);	
	LCD_WR_DATA(xStar>>8);
	LCD_WR_DATA(xStar);		
	LCD_WR_DATA(xEnd>>8);
	LCD_WR_DATA(xEnd);

	LCD_WR_REG(lcddev.setycmd);	
	LCD_WR_DATA(yStar>>8);
	LCD_WR_DATA(yStar);		
	LCD_WR_DATA(yEnd>>8);
	LCD_WR_DATA(yEnd);

	LCD_WriteRAM_Prepare();	//开始写入GRAM			
}   

/*****************************************************************************
 * @name       :void LCD_SetCursor(u16 Xpos, u16 Ypos)
 * @date       :2018-08-09 
 * @function   :Set coordinate value
 * @parameters :Xpos:the  x coordinate of the pixel
								Ypos:the  y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{	  	    			
	LCD_SetWindows(Xpos,Ypos,Xpos,Ypos);	
} 

/*****************************************************************************
 * @name       :void LCD_direction(u8 direction)
 * @date       :2018-08-09 
 * @function   :Setting the display direction of LCD screen
 * @parameters :direction:0-0 degree
                          1-90 degree
													2-180 degree
													3-270 degree
 * @retvalue   :None
******************************************************************************/ 
void LCD_direction(u8 direction)
{ 
			lcddev.setxcmd=0x2A;
			lcddev.setycmd=0x2B;
			lcddev.wramcmd=0x2C;
	switch(direction){		  
		case 0:						 	 		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;	
			LCD_WriteReg(0x36,0);//BGR==1,MY==0,MX==0,MV==0
		break;
		case 1:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<6)|(1<<5));//BGR==1,MY==1,MX==0,MV==1
		break;
		case 2:						 	 		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;;			
			LCD_WriteReg(0x36,(1<<6)|(1<<7));//BGR==1,MY==0,MX==0,MV==0
		break;
		case 3:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<7)|(1<<5));//BGR==1,MY==1,MX==0,MV==1
		break;	
		default:break;
	}		
}

/**
 *@name LCD_DisplayWindows(u16 Xstart, u16 Ystart, u16 Xend, u16 Yend, u16 *Image)
 *@function 写入颜色数据
 *@return none
 */
//void LCD_DisplayWindows(u16 Xstart, u16 Ystart, u16 Xend, u16 Yend, u16 *Image)
//{
//	u32 totalpoint = (Xend-Xstart+1)*(Yend-Ystart+1);
//	LCD_SetWindows(Xstart, Ystart, Xend , Yend);
//	
//	LCD_RS_SET;
//	LCD_CS_CLR;
//	LCD_WRITE_PACK((uint8_t*)Image, totalpoint<<1);
//}

// 刷新屏幕到硬件（横屏 320x240 正确）
void LCD_DisplayArea(u16 xStart, u16 yStart, u16 xEnd, u16 yEnd, uint8_t *data)
{
    uint32_t total_len;
    uint32_t offset = 0;

    if(data == NULL || xStart > xEnd || yStart > yEnd) return;
    if(xEnd >= lcddev.width || yEnd >= lcddev.height) return;

    total_len = ((uint32_t)(xEnd - xStart + 1) * (yEnd - yStart + 1)) << 1;
    LCD_SetWindows(xStart, yStart, xEnd, yEnd);

    LCD_CS_CLR;
    LCD_RS_SET;

    while(offset < total_len)
    {
        uint16_t chunk = (total_len - offset) > 0xFFFF ? 0xFFFF : (uint16_t)(total_len - offset);

        HAL_SPI_Transmit_DMA(lcd_spi, data + offset, chunk);
        while (HAL_SPI_GetState(lcd_spi) != HAL_SPI_STATE_READY);

        offset += chunk;
    }

    LCD_CS_SET;
}

void LCD_Flush(uint8_t* screen_buff)
{
    // 设置全屏窗口（横屏）
    LCD_SetWindows(0, 0, LCD_H-1, LCD_W-1);
    
    LCD_CS_CLR;
    LCD_RS_SET;

    uint32_t total_len = ST7789s_ScreenBufferSize; // 320*240*2 = 153600
    uint16_t chunk1 = 65535;
    uint16_t chunk2 = 65535;
    uint16_t chunk3 = total_len - chunk1 - chunk2;

    // 第一段
    HAL_SPI_Transmit_DMA(lcd_spi, screen_buff, chunk1);
    while (HAL_SPI_GetState(lcd_spi) != HAL_SPI_STATE_READY);
    
    // 第二段
    HAL_SPI_Transmit_DMA(lcd_spi, screen_buff + chunk1, chunk2);
    while (HAL_SPI_GetState(lcd_spi) != HAL_SPI_STATE_READY);
    
    // 第三段
    HAL_SPI_Transmit_DMA(lcd_spi, screen_buff + chunk1 + chunk2, chunk3);
    while (HAL_SPI_GetState(lcd_spi) != HAL_SPI_STATE_READY);

    LCD_CS_SET;
}

static void LCD_BuffWrite_HalfWord(u16 x, u16 y, u16 color, uint8_t* buff)
{
    // 横屏公式：偏移 = (y * 宽度 + x) * 2
    uint32_t index = ((uint32_t)y * LCD_H + x) * 2;
    
    buff[index]     = (color >> 8) & 0xFF;
    buff[index + 1] = color & 0xFF;
}

void LCD_LineBufFlushForNes(u8* buf,u16 len)
{
	LCD_NesBeginFrame();
	LCD_NesWriteAsync(buf, len);
	LCD_NesEndFrame();
}

void LCD_NesBeginFrame(void)
{
	(void)LCD_WaitForSPI();
	LCD_CS_CLR;
	LCD_RS_SET;
}

int LCD_NesWriteAsync(u8* buf, u16 len)
{
	if((buf == NULL) || (len == 0))
	{
		return 1;
	}

	if(LCD_WaitForSPI() != 0)
	{
		return 1;
	}

	return (HAL_SPI_Transmit_DMA(lcd_spi, buf, len) == HAL_OK) ? 0 : 1;
}

void LCD_NesWaitTransfer(void)
{
	(void)LCD_WaitForSPI();
}

void LCD_NesEndFrame(void)
{
	(void)LCD_WaitForSPI();
	LCD_CS_SET;
}

static int LCD_WaitForSPI(void)
{
	u32 count = 65536000;
	
	while (HAL_SPI_GetState(lcd_spi) != HAL_SPI_STATE_READY)
	{
		count --;
		
		if(!count)
		{
			return 1;
		}
	}
	
	return 0;
}

static void LCD_BuffWrite(u16 x_pos, u16 y_pos, u8* data, u16 len, uint8_t* buff)
{
    uint32_t offset = ((uint32_t)y_pos * LCD_H + x_pos) * 2;
	
    for(u16 i = 0; i < len; i++)
    {
        buff[offset + i] = data[i];
    }
}

void LCD_Fill(u16 xsta, u16 ysta, u16 xend, u16 yend, u16 color, uint8_t* buff)
{          
    u16 i, j;
    
    for(i = ysta; i <= yend; i++)
    {
        for(j = xsta; j <= xend; j++)
        {
            LCD_BuffWrite_HalfWord(j, i, color, buff);
        }
    }
}

//向指定帧缓冲区写入一个字符
void LCD_ShowChar(u16 x,u16 y,u8 num,u16 fc,u16 bc,u8 sizey,u8 mode,u8* screen_buff)
{
	u8 temp;
	u8 sizex = sizey / 2;          // 字符宽度
	u16 TypefaceNum;               // 一个字符所占字节大小
	u16 i, t;
	u16 x_pos = x, y_pos = y;      // 当前绘制坐标

	// 计算字体占用字节数
	TypefaceNum = ((sizex + 7) / 8) * sizey;
	num = num - ' ';               // 得到偏移后的值

	// 定义行缓冲：一行的像素 RGB565 颜色
	u8 line_buf[LCD_W * 2] = {0};
	u16 buf_idx = 0;

	for (i = 0; i < TypefaceNum; i++)
	{
		// 读取字库数据
		if (sizey == 12)      temp = ascii_1206[num][i];
		else if (sizey == 16) temp = ascii_1608[num][i];
		else if (sizey == 24) temp = ascii_2412[num][i];
		else if (sizey == 32) temp = ascii_3216[num][i];
		else return;

		// 逐位解析像素
		for (t = 0; t < 8; t++)
		{
			u16 color = (temp & (1 << t)) ? fc : bc;

			if (!mode) // 非叠加模式（正常背景）
			{
				// 写入 RGB565 颜色到行缓冲
				line_buf[buf_idx * 2]     = (color >> 8) & 0xFF;
				line_buf[buf_idx * 2 + 1] = color & 0xFF;
				buf_idx++;

				// 一行写完，刷到显存
				if (buf_idx >= sizex)
				{
					LCD_BuffWrite(x_pos, y_pos, line_buf, sizex * 2, screen_buff);
					buf_idx = 0;
					y_pos++;
					break;
				}
			}
			else // 叠加模式（透明背景）
			{
				if (temp & (1 << t))
				{
					// 写一个像素到显存
					u8 pix_buf[2] = {(color >> 8), color & 0xFF};
					LCD_BuffWrite(x_pos, y_pos, pix_buf, 2, screen_buff);
				}
				x_pos++;

				// 一行写完
				if (x_pos - x >= sizex)
				{
					x_pos = x;
					y_pos++;
					break;
				}
			}
		}
	}
}

// 显示字符串
void LCD_ShowString(u16 x,u16 y,const u8 *p,u16 fc,u16 bc,u8 sizey,u8 mode,uint8_t* screen_buff)
{
	while(*p != '\0')
	{
		LCD_ShowChar(x, y, *p, fc, bc, sizey, mode, screen_buff);
		x += sizey / 2;  // 字符宽度 = 字高的一半
		p++;
	}
}

void LCD_DrawBox(u16 x, u16 y, u16 width, u16 height,
                 u16 border_color, u16 bg_color, uint8_t* buff)
{
    LCD_Fill(x, y, x + width - 1, y + height - 1, bg_color, buff);

    LCD_Fill(x, y, x + width - 1, y, border_color, buff);

    LCD_Fill(x, y + height - 1, x + width - 1, y + height - 1, border_color, buff);

    LCD_Fill(x, y, x, y + height - 1, border_color, buff);

    LCD_Fill(x + width - 1, y, x + width - 1, y + height - 1, border_color, buff);
}

static void reverse(char *str) {
    int len = strlen(str);
    int i = 0, j = len - 1;
    while (i < j) {
        char temp = str[i];
        str[i++] = str[j];
        str[j--] = temp;
    }
}

static int intToStr(int32_t num, char *str) {
    int i = 0;
    uint8_t neg = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return i;
    }

    if (num < 0) {
        neg = 1;
        num = -num;
    }

    while (num != 0) {
        int rem = num % 10;
        str[i++] = rem + '0';
        num = num / 10;
    }

    if (neg) str[i++] = '-';
    str[i] = '\0';
    reverse(str);
    return i;
}

static void floatToStr(float num, char *str, uint8_t decimals) {
    int32_t intPart = (int32_t)num;
    float fracPart = num - intPart;
    int idx = 0;

    if (num < 0 && intPart == 0) {
        str[idx++] = '-';
        intPart = 0;
        fracPart = -fracPart;
    }

    idx += intToStr(intPart, str + idx);

    if (decimals <= 0) {
        str[idx] = '\0';
        return;
    }

    str[idx++] = '.';
    if (fracPart < 0) fracPart = -fracPart;

    for (uint8_t i = 0; i < decimals; i++) {
        fracPart *= 10;
        uint8_t digit = (uint8_t)fracPart;
        str[idx++] = digit + '0';
        fracPart -= digit;
    }
    str[idx] = '\0';
}

void LCD_ShowInt(u16 x, u16 y, int32_t num, 
                 u16 fc, u16 bc, u8 sizey, u8 mode, uint8_t *screen_buff)
{
    char buf[16];
    intToStr(num, buf);
    LCD_ShowString(x, y, (u8*)buf, fc, bc, sizey, mode, screen_buff);
}

void LCD_ShowFloat(u16 x, u16 y, float num, uint8_t decimals,
                    u16 fc, u16 bc, u8 sizey, u8 mode, uint8_t *screen_buff)
{
    char buf[20];
    floatToStr(num, buf, decimals);
    LCD_ShowString(x, y, (u8*)buf, fc, bc, sizey, mode, screen_buff);
}

void LCD_DrawCircle(u16 x0, u16 y0, u16 r, u16 color, uint8_t* buff)
{
    int a = r, b = 0;
    int err = 0;

    while (a >= b)
    {
        LCD_BuffWrite_HalfWord(x0 + a, y0 + b, color, buff);
        LCD_BuffWrite_HalfWord(x0 + b, y0 + a, color, buff);
        LCD_BuffWrite_HalfWord(x0 - b, y0 + a, color, buff);
        LCD_BuffWrite_HalfWord(x0 - a, y0 + b, color, buff);
        LCD_BuffWrite_HalfWord(x0 - a, y0 - b, color, buff);
        LCD_BuffWrite_HalfWord(x0 - b, y0 - a, color, buff);
        LCD_BuffWrite_HalfWord(x0 + b, y0 - a, color, buff);
        LCD_BuffWrite_HalfWord(x0 + a, y0 - b, color, buff);

        b++;
        if ((err += 2 * b + 1) > 0)
        {
            a--;
            err -= 2 * a + 1;
        }
    }
}

void LCD_DrawFillCircle(u16 x0, u16 y0, u16 r, u16 color, uint8_t* buff)
{
    int a = r, b = 0;
    int err = 0;

    while (a >= b)
    {
        LCD_Fill(x0 - a, y0 + b, x0 + a, y0 + b, color, buff);
        LCD_Fill(x0 - a, y0 - b, x0 + a, y0 - b, color, buff);
        LCD_Fill(x0 - b, y0 + a, x0 + b, y0 + a, color, buff);
        LCD_Fill(x0 - b, y0 - a, x0 + b, y0 - a, color, buff);

        b++;
        if ((err += 2 * b + 1) > 0)
        {
            a--;
            err -= 2 * a + 1;
        }
    }
}

void LCD_Line(u16 x1, u16 y1, u16 x2, u16 y2, u16 color, uint8_t* buff)
{
    int dx = (x2 >= x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 >= y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int x = x1, y = y1;

    while (1)
    {
        LCD_BuffWrite_HalfWord(x, y, color, buff);
        if (x == x2 && y == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y += sy;
        }
    }
}
