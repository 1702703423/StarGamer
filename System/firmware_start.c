/**
 * @file test.c
 * @brief Application test code — System init, LCD, bsp, mount_fs, HardFault handler
 */
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "ff.h"
#include "stdio.h"
#include "string.h"
#include "Player.h"
#include "System_Status.h"
#include "Malloc.h"
#include "LCD.h"
#include "dl_lib.h"
#include "nes_main.h"
#include "shell.h"
#include "shell_support.h"
#include "test.h"
#include "Player.h"
#include "w25q64.h"
#include "key.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "../GUI/GUI_Main.h"

/* External globals */
extern Shell shell;
extern System_Resource_Table_t SRT;
extern System_Status_Information_t SSI;
extern System_Core_Information_t SCI;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi1;
extern int AxfDebugInfoPushToUart(const char *string, va_list arg);
extern void shell_fs_init(void);
extern void lv_port_indev_init(void);

/* ---- LVGL timer handler thread ---- */
#define LVGL_THREAD_STACK_SIZE  2048
typedef StaticTask_t osStaticThreadDef_t;
static uint32_t lvglTaskBuffer[LVGL_THREAD_STACK_SIZE];
static osStaticThreadDef_t lvglTaskControlBlock;
static const osThreadAttr_t lvglTask_attributes = {
	.name = "LVGL",
	.cb_mem = &lvglTaskControlBlock,
	.cb_size = sizeof(lvglTaskControlBlock),
	.stack_mem = &lvglTaskBuffer[0],
	.stack_size = sizeof(lvglTaskBuffer),
	.priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t lvglThreadId = NULL;

/* ---- LVGL timer handler task ---- */
static void lvgl_timer_task(void *arg)
{
	(void)arg;
	while (1) {
		lv_timer_handler();
		osDelay(10);
	}
}

static int key_init()
{
	return KEY_Init();
}

/* ---- System Init ---- */
void System_init()
{
	int code;
	
	//内存池初始化
	sys_mem_init(0);
	sys_mem_init(1);
	
	//开发阶段，调试模式常开
	SCI.Debug = 1;

	shell.read = shell_read;
	shell.write = shell_write;

	if (SCI.Debug) {
		shell_lock_create();
		shellInit(&shell, shell_buffer, SHELL_BUFFER_SIZE);
		shell_uart_rx_start();
		osThreadNew(shellTask, &shell, &shellTask_attributes);
		shell_fs_init();
	}

	SRT.frame_buffer_table = creat_chain(0, &code);
	SRT.task_table = creat_chain(0, &code);
	SRT.task_table = creat_chain(0,&code);
	
	creat_frame_buffer(&code);/*本项目架构中默认第一缓冲区为LVGL缓冲*/
	
}

/* ---- HardFault Handler (C part) ---- */
#define HARDFAULT_LCD_WIDTH          320U
#define HARDFAULT_LCD_HEIGHT         240U
#define HARDFAULT_FONT_WIDTH         5U
#define HARDFAULT_FONT_HEIGHT        7U
#define HARDFAULT_FONT_STEP          6U
#define HARDFAULT_SPI_TIMEOUT        1000000U
#define HARDFAULT_COLOR_BLACK        0x0000U
#define HARDFAULT_COLOR_WHITE        0xFFFFU
#define HARDFAULT_COLOR_RED_DARK     0x7800U
#define HARDFAULT_COLOR_YELLOW       0xFFE0U
#define HARDFAULT_COLOR_CYAN         0x07FFU

#define HARDFAULT_GPIO_MODE_MASK(pin)     (3UL << ((pin) * 2U))
#define HARDFAULT_GPIO_MODE_AF(pin)       (2UL << ((pin) * 2U))
#define HARDFAULT_GPIO_MODE_OUT(pin)      (1UL << ((pin) * 2U))
#define HARDFAULT_GPIO_SPEED_HIGH(pin)    (3UL << ((pin) * 2U))
#define HARDFAULT_GPIO_AFR_MASK(pin)      (0xFUL << ((pin) * 4U))
#define HARDFAULT_GPIO_AFR_AF5(pin)       (5UL << ((pin) * 4U))

#define HARDFAULT_LCD_CS_HIGH()           (GPIOC->BSRR = (u32)LCD_CS_Pin)
#define HARDFAULT_LCD_CS_LOW()            (GPIOC->BSRR = ((u32)LCD_CS_Pin << 16U))
#define HARDFAULT_LCD_RS_HIGH()           (GPIOC->BSRR = (u32)LCD_RS_Pin)
#define HARDFAULT_LCD_RS_LOW()            (GPIOC->BSRR = ((u32)LCD_RS_Pin << 16U))
#define HARDFAULT_LCD_RST_HIGH()          (GPIOB->BSRR = GPIO_PIN_12)
#define HARDFAULT_LCD_RST_LOW()           (GPIOB->BSRR = ((u32)GPIO_PIN_12 << 16U))
#define HARDFAULT_LCD_BL_HIGH()           (GPIOB->BSRR = GPIO_PIN_15)

typedef struct {
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
} HardFault_Record_t;

typedef struct {
	char ch;
	u8 col[HARDFAULT_FONT_WIDTH];
} HardFault_Font5x7_t;

volatile HardFault_Record_t g_hardfault_record;

static volatile u8 hardfault_lcd_busy;
static char hardfault_line[64];

static const HardFault_Font5x7_t hardfault_font5x7[] = {
	{' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
	{'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
	{'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
	{'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
	{'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
	{'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
	{'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
	{'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
	{'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
	{'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
	{'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
	{'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
	{'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
	{'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
	{'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
	{'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
	{'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
	{'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
	{'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
	{'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
	{'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
	{'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
	{'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
	{'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
	{'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
	{'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
	{'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
	{'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
	{'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
	{'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
	{'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
	{'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
	{'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
	{'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
	{'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
	{'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
	{'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
	{':', {0x00, 0x36, 0x36, 0x00, 0x00}},
	{'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
	{'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
	{'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
};

static void hardfault_delay(volatile u32 count)
{
	while(count--)
	{
		__NOP();
	}
}

static u8 hardfault_addr_readable(u32 addr, u32 bytes)
{
	u32 end = addr + bytes - 1U;

	if((bytes == 0U) || ((addr & 3U) != 0U) || (end < addr))
	{
		return 0U;
	}

	if((addr >= 0x10000000U) && (end <= 0x1000FFFFU))
	{
		return 1U;
	}

	if((addr >= 0x20000000U) && (end <= 0x2001FFFFU))
	{
		return 1U;
	}

	if((addr >= 0x68000000U) && (end <= 0x680FFFFFU))
	{
		return 1U;
	}

	return 0U;
}

static void hardfault_dma_stop(DMA_Stream_TypeDef *stream)
{
	u32 timeout = HARDFAULT_SPI_TIMEOUT;

	stream->CR &= ~DMA_SxCR_EN;
	while(((stream->CR & DMA_SxCR_EN) != 0U) && (timeout != 0U))
	{
		timeout--;
	}
}

static void hardfault_lcd_gpio_spi_init(void)
{
	u32 mask;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
	                RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_DMA2EN;
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
	__DSB();

	SPI1->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_TXEIE |
	               SPI_CR2_RXNEIE | SPI_CR2_ERRIE);
	hardfault_dma_stop(DMA2_Stream5);
	hardfault_dma_stop(DMA2_Stream0);

	SPI1->CR1 &= ~SPI_CR1_SPE;
	RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
	RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;

	mask = HARDFAULT_GPIO_MODE_MASK(5U) |
	       HARDFAULT_GPIO_MODE_MASK(6U) |
	       HARDFAULT_GPIO_MODE_MASK(7U);
	GPIOA->MODER = (GPIOA->MODER & ~mask) |
	               HARDFAULT_GPIO_MODE_AF(5U) |
	               HARDFAULT_GPIO_MODE_AF(6U) |
	               HARDFAULT_GPIO_MODE_AF(7U);
	GPIOA->OTYPER &= ~(GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
	GPIOA->OSPEEDR |= HARDFAULT_GPIO_SPEED_HIGH(5U) |
	                  HARDFAULT_GPIO_SPEED_HIGH(6U) |
	                  HARDFAULT_GPIO_SPEED_HIGH(7U);
	GPIOA->PUPDR &= ~mask;
	mask = HARDFAULT_GPIO_AFR_MASK(5U) |
	       HARDFAULT_GPIO_AFR_MASK(6U) |
	       HARDFAULT_GPIO_AFR_MASK(7U);
	GPIOA->AFR[0] = (GPIOA->AFR[0] & ~mask) |
	                HARDFAULT_GPIO_AFR_AF5(5U) |
	                HARDFAULT_GPIO_AFR_AF5(6U) |
	                HARDFAULT_GPIO_AFR_AF5(7U);

	mask = HARDFAULT_GPIO_MODE_MASK(4U) | HARDFAULT_GPIO_MODE_MASK(5U);
	GPIOC->MODER = (GPIOC->MODER & ~mask) |
	               HARDFAULT_GPIO_MODE_OUT(4U) |
	               HARDFAULT_GPIO_MODE_OUT(5U);
	GPIOC->OTYPER &= ~(LCD_CS_Pin | LCD_RS_Pin);
	GPIOC->OSPEEDR |= HARDFAULT_GPIO_SPEED_HIGH(4U) |
	                  HARDFAULT_GPIO_SPEED_HIGH(5U);
	GPIOC->PUPDR &= ~mask;
	HARDFAULT_LCD_CS_HIGH();
	HARDFAULT_LCD_RS_HIGH();

	mask = HARDFAULT_GPIO_MODE_MASK(12U) | HARDFAULT_GPIO_MODE_MASK(15U);
	GPIOB->MODER = (GPIOB->MODER & ~mask) |
	               HARDFAULT_GPIO_MODE_OUT(12U) |
	               HARDFAULT_GPIO_MODE_OUT(15U);
	GPIOB->OTYPER &= ~(GPIO_PIN_12 | GPIO_PIN_15);
	GPIOB->OSPEEDR |= HARDFAULT_GPIO_SPEED_HIGH(12U) |
	                  HARDFAULT_GPIO_SPEED_HIGH(15U);
	GPIOB->PUPDR &= ~mask;
	HARDFAULT_LCD_RST_HIGH();
	HARDFAULT_LCD_BL_HIGH();

	SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_1;
	SPI1->CR2 = 0U;
	SPI1->CR1 |= SPI_CR1_SPE;
}

static u8 hardfault_spi_wait_set(u32 flag)
{
	u32 timeout = HARDFAULT_SPI_TIMEOUT;

	while(((SPI1->SR & flag) == 0U) && (timeout != 0U))
	{
		timeout--;
	}

	return (timeout != 0U) ? 1U : 0U;
}

static void hardfault_spi_wait_clear(u32 flag)
{
	u32 timeout = HARDFAULT_SPI_TIMEOUT;

	while(((SPI1->SR & flag) != 0U) && (timeout != 0U))
	{
		timeout--;
	}
}

static void hardfault_spi_write8(u8 data)
{
	u32 timeout = HARDFAULT_SPI_TIMEOUT;

	if(hardfault_spi_wait_set(SPI_SR_TXE) == 0U)
	{
		return;
	}

	*(__IO u8 *)&SPI1->DR = data;
	while(((SPI1->SR & SPI_SR_RXNE) == 0U) && (timeout != 0U))
	{
		timeout--;
	}

	if((SPI1->SR & SPI_SR_RXNE) != 0U)
	{
		(void)*(__IO u8 *)&SPI1->DR;
	}
}

static void hardfault_spi_finish(void)
{
	(void)hardfault_spi_wait_set(SPI_SR_TXE);
	hardfault_spi_wait_clear(SPI_SR_BSY);

	if((SPI1->SR & SPI_SR_RXNE) != 0U)
	{
		(void)*(__IO u8 *)&SPI1->DR;
	}
	(void)SPI1->SR;
}

static void hardfault_lcd_write_cmd(u8 command)
{
	HARDFAULT_LCD_CS_LOW();
	HARDFAULT_LCD_RS_LOW();
	hardfault_spi_write8(command);
	hardfault_spi_finish();
	HARDFAULT_LCD_CS_HIGH();
}

static void hardfault_lcd_write_data8(u8 data)
{
	HARDFAULT_LCD_CS_LOW();
	HARDFAULT_LCD_RS_HIGH();
	hardfault_spi_write8(data);
	hardfault_spi_finish();
	HARDFAULT_LCD_CS_HIGH();
}

static void hardfault_lcd_set_window(u16 x0, u16 y0, u16 x1, u16 y1)
{
	hardfault_lcd_write_cmd(0x2AU);
	hardfault_lcd_write_data8((u8)(x0 >> 8));
	hardfault_lcd_write_data8((u8)x0);
	hardfault_lcd_write_data8((u8)(x1 >> 8));
	hardfault_lcd_write_data8((u8)x1);

	hardfault_lcd_write_cmd(0x2BU);
	hardfault_lcd_write_data8((u8)(y0 >> 8));
	hardfault_lcd_write_data8((u8)y0);
	hardfault_lcd_write_data8((u8)(y1 >> 8));
	hardfault_lcd_write_data8((u8)y1);

	hardfault_lcd_write_cmd(0x2CU);
}

static void hardfault_lcd_fill_rect(u16 x, u16 y, u16 width, u16 height, u16 color)
{
	u32 count;

	if((width == 0U) || (height == 0U) ||
	   (x >= HARDFAULT_LCD_WIDTH) || (y >= HARDFAULT_LCD_HEIGHT))
	{
		return;
	}

	if((u32)x + width > HARDFAULT_LCD_WIDTH)
	{
		width = (u16)(HARDFAULT_LCD_WIDTH - x);
	}
	if((u32)y + height > HARDFAULT_LCD_HEIGHT)
	{
		height = (u16)(HARDFAULT_LCD_HEIGHT - y);
	}

	hardfault_lcd_set_window(x, y, (u16)(x + width - 1U), (u16)(y + height - 1U));

	HARDFAULT_LCD_CS_LOW();
	HARDFAULT_LCD_RS_HIGH();
	count = (u32)width * height;
	while(count--)
	{
		hardfault_spi_write8((u8)(color >> 8));
		hardfault_spi_write8((u8)color);
	}
	hardfault_spi_finish();
	HARDFAULT_LCD_CS_HIGH();
}

static void hardfault_lcd_init_direct(void)
{
	hardfault_lcd_gpio_spi_init();

	HARDFAULT_LCD_RST_LOW();
	hardfault_delay(500000U);
	HARDFAULT_LCD_RST_HIGH();
	hardfault_delay(500000U);

	hardfault_lcd_write_cmd(0x01U);
	hardfault_delay(500000U);
	hardfault_lcd_write_cmd(0x11U);
	hardfault_delay(1000000U);
	hardfault_lcd_write_cmd(0x36U);
	hardfault_lcd_write_data8(0x60U);
	hardfault_lcd_write_cmd(0x3AU);
	hardfault_lcd_write_data8(0x05U);
	hardfault_lcd_write_cmd(0x21U);
	hardfault_lcd_write_cmd(0x29U);
	hardfault_delay(500000U);
}

static const u8 *hardfault_font_get(char ch)
{
	u32 i;

	if((ch >= 'a') && (ch <= 'z'))
	{
		ch = (char)(ch - ('a' - 'A'));
	}

	for(i = 0U; i < (sizeof(hardfault_font5x7) / sizeof(hardfault_font5x7[0])); i++)
	{
		if(hardfault_font5x7[i].ch == ch)
		{
			return hardfault_font5x7[i].col;
		}
	}

	return hardfault_font5x7[sizeof(hardfault_font5x7) / sizeof(hardfault_font5x7[0]) - 1U].col;
}

static void hardfault_lcd_write_color(u16 color)
{
	hardfault_spi_write8((u8)(color >> 8));
	hardfault_spi_write8((u8)color);
}

static void hardfault_lcd_draw_char(u16 x, u16 y, char ch, u16 fg, u16 bg, u8 scale)
{
	const u8 *glyph = hardfault_font_get(ch);
	u8 col;
	u8 row;
	u8 x_scale;
	u8 y_scale;

	if(scale == 0U)
	{
		scale = 1U;
	}

	if(((u32)x + HARDFAULT_FONT_WIDTH * scale) > HARDFAULT_LCD_WIDTH ||
	   ((u32)y + HARDFAULT_FONT_HEIGHT * scale) > HARDFAULT_LCD_HEIGHT)
	{
		return;
	}

	hardfault_lcd_set_window(x, y,
	                         (u16)(x + HARDFAULT_FONT_WIDTH * scale - 1U),
	                         (u16)(y + HARDFAULT_FONT_HEIGHT * scale - 1U));

	HARDFAULT_LCD_CS_LOW();
	HARDFAULT_LCD_RS_HIGH();
	for(row = 0U; row < HARDFAULT_FONT_HEIGHT; row++)
	{
		for(y_scale = 0U; y_scale < scale; y_scale++)
		{
			for(col = 0U; col < HARDFAULT_FONT_WIDTH; col++)
			{
				for(x_scale = 0U; x_scale < scale; x_scale++)
				{
					hardfault_lcd_write_color(((glyph[col] & (1U << row)) != 0U) ? fg : bg);
				}
			}
		}
	}
	hardfault_spi_finish();
	HARDFAULT_LCD_CS_HIGH();
}

static void hardfault_lcd_puts(u16 x, u16 y, const char *text, u16 fg, u16 bg, u8 scale)
{
	while((*text != '\0') && (x < HARDFAULT_LCD_WIDTH))
	{
		hardfault_lcd_draw_char(x, y, *text, fg, bg, scale);
		x = (u16)(x + HARDFAULT_FONT_STEP * scale);
		text++;
	}
}

static char *hardfault_append_str(char *dst, const char *src)
{
	while(*src != '\0')
	{
		*dst++ = *src++;
	}

	return dst;
}

static char *hardfault_append_hex32(char *dst, u32 value)
{
	static const char hex[] = "0123456789ABCDEF";
	s8 i;

	*dst++ = '0';
	*dst++ = 'X';
	for(i = 7; i >= 0; i--)
	{
		*dst++ = hex[(value >> ((u32)i * 4U)) & 0x0FU];
	}

	return dst;
}

static void hardfault_make_pair_line(const char *left_label, u32 left_value,
                                     const char *right_label, u32 right_value)
{
	char *p = hardfault_line;

	p = hardfault_append_str(p, left_label);
	*p++ = ':';
	p = hardfault_append_hex32(p, left_value);
	if(right_label != NULL)
	{
		*p++ = ' ';
		p = hardfault_append_str(p, right_label);
		*p++ = ':';
		p = hardfault_append_hex32(p, right_value);
	}
	*p = '\0';
}

static void hardfault_lcd_print_line(u16 *y, const char *text, u16 color)
{
	if(*y > (HARDFAULT_LCD_HEIGHT - 8U))
	{
		return;
	}

	hardfault_lcd_puts(4U, *y, text, color, HARDFAULT_COLOR_BLACK, 1U);
	*y = (u16)(*y + 9U);
}

static void hardfault_lcd_print_record_line(u16 *y, const char *left_label, u32 left_value,
                                            const char *right_label, u32 right_value,
                                            u16 color)
{
	hardfault_make_pair_line(left_label, left_value, right_label, right_value);
	hardfault_lcd_print_line(y, hardfault_line, color);
}

static void hardfault_lcd_print_frame_mode(u16 *y, volatile const HardFault_Record_t *record)
{
	char *p = hardfault_line;

	p = hardfault_append_str(p, "STACK:");
	p = hardfault_append_str(p, ((record->exc_return & (1UL << 2)) != 0U) ? "PSP" : "MSP");
	p = hardfault_append_str(p, " FRAME:");
	p = hardfault_append_str(p, (record->fp_extended_frame != 0U) ? "EXTFP" : "BASIC");
	*p = '\0';

	hardfault_lcd_print_line(y, hardfault_line, HARDFAULT_COLOR_CYAN);
}

static void hardfault_lcd_print_flag(u16 *y, const char *text)
{
	hardfault_lcd_print_line(y, text, HARDFAULT_COLOR_YELLOW);
}

static void hardfault_lcd_print_fault_flags(u16 *y, volatile const HardFault_Record_t *record)
{
	u32 cfsr = record->cfsr;
	u32 hfsr = record->hfsr;

	if((hfsr & SCB_HFSR_FORCED_Msk) != 0U) hardfault_lcd_print_flag(y, "HFSR FORCED");
	if((hfsr & SCB_HFSR_VECTTBL_Msk) != 0U) hardfault_lcd_print_flag(y, "HFSR VECTTBL");
	if((hfsr & SCB_HFSR_DEBUGEVT_Msk) != 0U) hardfault_lcd_print_flag(y, "HFSR DEBUGEVT");

	if((cfsr & SCB_CFSR_IACCVIOL_Msk) != 0U) hardfault_lcd_print_flag(y, "MEM IACCVIOL");
	if((cfsr & SCB_CFSR_DACCVIOL_Msk) != 0U) hardfault_lcd_print_flag(y, "MEM DACCVIOL");
	if((cfsr & SCB_CFSR_MUNSTKERR_Msk) != 0U) hardfault_lcd_print_flag(y, "MEM MUNSTKERR");
	if((cfsr & SCB_CFSR_MSTKERR_Msk) != 0U) hardfault_lcd_print_flag(y, "MEM MSTKERR");
	if((cfsr & SCB_CFSR_MLSPERR_Msk) != 0U) hardfault_lcd_print_flag(y, "MEM MLSPERR");
	if((cfsr & SCB_CFSR_MMARVALID_Msk) != 0U) hardfault_lcd_print_flag(y, "MEM MMARVALID");

	if((cfsr & SCB_CFSR_IBUSERR_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS IBUSERR");
	if((cfsr & SCB_CFSR_PRECISERR_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS PRECISERR");
	if((cfsr & SCB_CFSR_IMPRECISERR_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS IMPRECISERR");
	if((cfsr & SCB_CFSR_UNSTKERR_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS UNSTKERR");
	if((cfsr & SCB_CFSR_STKERR_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS STKERR");
	if((cfsr & SCB_CFSR_LSPERR_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS LSPERR");
	if((cfsr & SCB_CFSR_BFARVALID_Msk) != 0U) hardfault_lcd_print_flag(y, "BUS BFARVALID");

	if((cfsr & SCB_CFSR_UNDEFINSTR_Msk) != 0U) hardfault_lcd_print_flag(y, "USG UNDEFINSTR");
	if((cfsr & SCB_CFSR_INVSTATE_Msk) != 0U) hardfault_lcd_print_flag(y, "USG INVSTATE");
	if((cfsr & SCB_CFSR_INVPC_Msk) != 0U) hardfault_lcd_print_flag(y, "USG INVPC");
	if((cfsr & SCB_CFSR_NOCP_Msk) != 0U) hardfault_lcd_print_flag(y, "USG NOCP");
	if((cfsr & SCB_CFSR_UNALIGNED_Msk) != 0U) hardfault_lcd_print_flag(y, "USG UNALIGNED");
	if((cfsr & SCB_CFSR_DIVBYZERO_Msk) != 0U) hardfault_lcd_print_flag(y, "USG DIVBYZERO");
}

static void hardfault_lcd_show(volatile const HardFault_Record_t *record)
{
	u16 y = 30U;

	hardfault_lcd_init_direct();
	hardfault_lcd_fill_rect(0U, 0U, HARDFAULT_LCD_WIDTH, HARDFAULT_LCD_HEIGHT,
	                        HARDFAULT_COLOR_BLACK);
	hardfault_lcd_fill_rect(0U, 0U, HARDFAULT_LCD_WIDTH, 24U, HARDFAULT_COLOR_RED_DARK);
	hardfault_lcd_puts(6U, 4U, "HARDFAULT", HARDFAULT_COLOR_WHITE,
	                   HARDFAULT_COLOR_RED_DARK, 2U);

	hardfault_lcd_print_frame_mode(&y, record);
	hardfault_lcd_print_record_line(&y, "EXC", record->exc_return,
	                                "SP", record->stack_pointer, HARDFAULT_COLOR_WHITE);
	hardfault_lcd_print_record_line(&y, "FRM", record->frame_pointer,
	                                "VALID", record->frame_valid, HARDFAULT_COLOR_WHITE);
	hardfault_lcd_print_record_line(&y, "MSP", record->msp,
	                                "PSP", record->psp, HARDFAULT_COLOR_WHITE);
	hardfault_lcd_print_record_line(&y, "CFSR", record->cfsr,
	                                "HFSR", record->hfsr, HARDFAULT_COLOR_YELLOW);
	hardfault_lcd_print_record_line(&y, "DFSR", record->dfsr,
	                                "AFSR", record->afsr, HARDFAULT_COLOR_WHITE);
	hardfault_lcd_print_record_line(&y, "MMFAR", record->mmfar,
	                                "BFAR", record->bfar, HARDFAULT_COLOR_WHITE);

	if(record->frame_valid != 0U)
	{
		hardfault_lcd_print_record_line(&y, "PC", record->pc,
		                                "LR", record->stacked_lr, HARDFAULT_COLOR_CYAN);
		hardfault_lcd_print_record_line(&y, "XPSR", record->xpsr,
		                                "R12", record->r12, HARDFAULT_COLOR_WHITE);
		hardfault_lcd_print_record_line(&y, "R0", record->r0,
		                                "R1", record->r1, HARDFAULT_COLOR_WHITE);
		hardfault_lcd_print_record_line(&y, "R2", record->r2,
		                                "R3", record->r3, HARDFAULT_COLOR_WHITE);
	}
	else
	{
		hardfault_lcd_print_line(&y, "STACK FRAME INVALID", HARDFAULT_COLOR_YELLOW);
	}

	hardfault_lcd_print_fault_flags(&y, record);
}

void HardFault_Handler_C(u32 *stack_pointer, u32 lr_value, u32 msp_value, u32 psp_value)
{
	u32 frame_addr;
	u32 *frame;

	__disable_irq();

	if(hardfault_lcd_busy != 0U)
	{
		while(1)
		{
			__NOP();
		}
	}
	hardfault_lcd_busy = 1U;

	g_hardfault_record.stack_pointer = (u32)stack_pointer;
	g_hardfault_record.exc_return = lr_value;
	g_hardfault_record.msp = msp_value;
	g_hardfault_record.psp = psp_value;
	g_hardfault_record.cfsr = SCB->CFSR;
	g_hardfault_record.hfsr = SCB->HFSR;
	g_hardfault_record.dfsr = SCB->DFSR;
	g_hardfault_record.afsr = SCB->AFSR;
	g_hardfault_record.mmfar = SCB->MMFAR;
	g_hardfault_record.bfar = SCB->BFAR;
	g_hardfault_record.fp_extended_frame = ((lr_value & (1UL << 4)) == 0U) ? 1U : 0U;
	frame_addr = (u32)stack_pointer + ((g_hardfault_record.fp_extended_frame != 0U) ? (18U * 4U) : 0U);
	g_hardfault_record.frame_pointer = frame_addr;
	g_hardfault_record.frame_valid = hardfault_addr_readable(frame_addr, 8U * sizeof(u32));

	if(g_hardfault_record.frame_valid != 0U)
	{
		frame = (u32 *)frame_addr;
		g_hardfault_record.r0 = frame[0];
		g_hardfault_record.r1 = frame[1];
		g_hardfault_record.r2 = frame[2];
		g_hardfault_record.r3 = frame[3];
		g_hardfault_record.r12 = frame[4];
		g_hardfault_record.stacked_lr = frame[5];
		g_hardfault_record.pc = frame[6];
		g_hardfault_record.xpsr = frame[7];
	}

	hardfault_lcd_show(&g_hardfault_record);

	while(1)
	{
		__NOP();
	}
}

/* ---- LCD test ---- */
void lcd_test()
{
	uint8_t *p = ((Frame_Buffer_Status_t *)(SRT.frame_buffer_table->next->next->data))->pointer;

	LCD_Fill(0, 0, 320, 120, BLUE, p);
	LCD_Fill(0, 120, 320, 240, RED, p);
	LCD_DrawBox(1, 1, 120, 60, YELLOW, GRAY, p);
	LCD_ShowFloat(10, 10, 18.372f, 2, YELLOW, GRAY, 16, 0, p);
	LCD_ShowString(1, 220, (const u8 *)"HelloWorld", YELLOW, RED, 16, 0, p);
	LCD_DrawFillCircle(160, 20, 20, YELLOW, p);
	LCD_DrawCircle(220, 20, 20, GREEN, p);
	LCD_Flush(p);
}

/* ---- GPIO init ---- */
static void gpio_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOE_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_6;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	/* Deselect W25Q64 (CS high) */
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_SET);
}

/* ---- Board support init ---- */
static void bsp_init()
{
	uint8 mid;
	uint16_t did;
	
	gpio_init();
	LCD_Init(&hspi1);
	key_init();
	if(W25Q64_Init() == W25Q64_OK)
	{
		W25Q64_ReadID(&mid,&did);
		shellPrintEndLine(&shell,"W25Q64InitSuccess,MID,DID:%X,%X\r\n",mid,did);
	}else
	{
		shellPrintEndLine(&shell,"W25Q64Failed\r\n");
	}
}

/* ---- Mount FATFS ---- */
int mount_fs()
{
	FRESULT fr;

	fr = f_mount(&SDFatFS, "0:", 1);

	if (fr == FR_OK) {
		SSI.fs_mount_status = 1;
	}

	return SSI.fs_mount_status;
}

/* ---- Main application entry (called from StartDefaultTask) ---- */

static void LVGL_shellLog(const char* log)
{
	shellPrintEndLine(&shell,log);
}

static void lvgl_init()
{
	lv_init();
	lv_port_disp_init();
	lv_port_indev_init();
	if(SCI.Debug)
	{
		lv_log_register_print_cb(LVGL_shellLog);
	}
	lvglThreadId = osThreadNew(lvgl_timer_task, NULL, &lvglTask_attributes);
}

static void GUI_Init(void)
{
	(void)GUI_Main_Init();
}

void init(void)
{
	int res;
	res = mount_fs();
	System_init();
	bsp_init();
		/* LVGL 初始化 */
	lvgl_init();
//	nes_load((u8*)"0://SICHUAN.NES");
	GUI_Init();
//	LCD_Fill(1, 1, 320, 240, BLACK, p);
//	LCD_Flush(p);lcd_test();

//	if (res)
//	{
//		DL_Handler DL;
//		int (*enterFunc)(int argc, char *argv[]);
//		DL_Err_Type err;

//		osKernelLock();
//		err = dl_load_file(&DL, "0:/debug.axf");
//		osKernelUnlock();

//		shellPrintEndLine(&shell, "err:%d\n", err);

//		if (err) {
//			while (1);
//		}

//		enterFunc = dl_get_entry(&DL);
//		shellPrintEndLine(&shell, "StartAddressAt:%p\n", enterFunc);

//		if (enterFunc == NULL) {
//			shellPrintEndLine(&shell, "cant find entry\n");
//		}

//		shellPrintEndLine(&shell, "NeedExecute:%p\n", AxfDebugInfoPushToUart);
//		shellPrintEndLine(&shell, "res:%d\n", enterFunc(0, NULL));
//		dl_destroy_lib(&DL);
//	}

	if (res) {
		while (1) {
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);
			osDelay(pdMS_TO_TICKS(1000));
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET);
			osDelay(pdMS_TO_TICKS(1000));
		}
	} else {
		while (1) {
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);
			osDelay(pdMS_TO_TICKS(200));
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET);
			osDelay(pdMS_TO_TICKS(200));
		}
	}
}
