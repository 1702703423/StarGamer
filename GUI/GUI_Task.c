#include "GUI_Task.h"

osThreadAttr_t GUI_Handler = {
	.name = "GUI_Handler",
	.priority = osPriorityAboveNormal,
	.stack_size = 2048
};