#include "sys.h"

uint32_t SysClockGet(void)
{
  uint32_t    PLLM = 0, PLLN = 0, PLLP = 0, PLLSRC = 0;
       
	if((RCC->CFGR & RCC_CFGR_SWS ) == RCC_CFGR_SWS_HSE) 
		return  HSE_VALUE;
	else if((RCC->CFGR & RCC_CFGR_SWS ) == RCC_CFGR_SWS_HSI) 
		return  HSI_VALUE;
	else                
	{
		PLLM 			= RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
		PLLN    	= ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN)>>6);
		PLLP    	= ((((RCC->PLLCFGR & RCC_PLLCFGR_PLLP)>>16)+1)<<1);
		PLLSRC  	= RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC;
		if(PLLSRC == 0)        
			return (((HSI_VALUE * PLLN) / PLLM )/ PLLP);
		else                                                
			return (((HSE_VALUE * PLLN) / PLLM )/ PLLP);
	}
}

//void delay_us(uint32_t us)
//{
//	uint16_t compare=(0XFFFF-us-5);
//	
//	__HAL_TIM_SET_PRESCALER(&htim6,84-1);		
//	htim6.Instance->EGR|=0x0001;				
//	__HAL_TIM_SetCounter(&htim6,compare);		
//	
//	HAL_TIM_Base_Start(&htim6);
//	while(compare<0XFFFF-5)
//	{
//		compare=__HAL_TIM_GetCounter(&htim6);
//	}
//	HAL_TIM_Base_Stop(&htim6);
//}

//void delay_ms(uint32_t ms)
//{
//	uint16_t compare=(0XFFFF-10*ms-5);
//	
//	__HAL_TIM_SET_PRESCALER(&htim6,8400-1);		
//	htim6.Instance->EGR|=0x0001;						
//	__HAL_TIM_SetCounter(&htim6,compare);
//	
//	HAL_TIM_Base_Start(&htim6);
//	
//	while(compare<0XFFFF-5)
//	{
//		compare=__HAL_TIM_GetCounter(&htim6);
//	}
//	HAL_TIM_Base_Stop(&htim6);
//}

void System_Reset(void)
{
	__set_FAULTMASK(1); //关闭所有中断
	NVIC_SystemReset();
}

//关闭所有中断(但是不包括fault和NMI中断)
void INTX_DISABLE(void)
{
	__ASM volatile("cpsid i");  
}
//开启所有中断
void INTX_ENABLE(void)
{
	__ASM volatile("cpsie i");
}
//设置栈顶地址
//addr:栈顶地址
void MSR_MSP(u32 addr) 
{
	__ASM volatile
	(
		"MSR MSP, r0\n\t"
		"BX r14"
	);
}














