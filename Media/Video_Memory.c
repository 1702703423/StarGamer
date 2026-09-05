#include "Video_Memory.h"

#include <string.h>

#define VIDEO_MEMORY_HEADER_SIZE       VIDEO_MEMORY_ALIGNMENT
#define VIDEO_MEMORY_OFFSET_NONE       0xFFFFFFFFUL
#define VIDEO_MEMORY_BLOCK_MAGIC       0x564D454DUL
#define VIDEO_MEMORY_BLOCK_FREE        0U
#define VIDEO_MEMORY_BLOCK_ALLOCATED   1U
#define VIDEO_MEMORY_ALIGN(size) \
	(((size) + VIDEO_MEMORY_ALIGNMENT - 1U) & ~(VIDEO_MEMORY_ALIGNMENT - 1U))

typedef struct Video_MemoryBlock
{
	u32 magic;
	u32 size;
	u32 next_offset;
	u32 previous_offset;
	u32 state;
	u32 reserved[3];
} Video_MemoryBlock_t;

typedef char Video_MemoryHeaderSizeCheck[
	(sizeof(Video_MemoryBlock_t) == VIDEO_MEMORY_HEADER_SIZE) ? 1 : -1];

static u32 video_memory_lock(void);
static void video_memory_unlock(u32 interrupt_mask);
static Video_MemoryBlock_t *video_memory_block(u32 offset);
static void video_memory_initialize_locked(void);
static u32 video_memory_largest_free_locked(void);

__attribute__((section(".bss.video_sram"), aligned(VIDEO_MEMORY_ALIGNMENT), used))
static u8 s_video_memory_pool[VIDEO_MEMORY_POOL_SIZE];

static u8 s_video_memory_initialized;
static u32 s_video_memory_current;
static u32 s_video_memory_peak;
static u32 s_video_memory_failures;

void Video_Memory_Init(void)
{
	u32 interrupt_mask = video_memory_lock();

	if(s_video_memory_initialized == 0U)
	{
		video_memory_initialize_locked();
	}
	video_memory_unlock(interrupt_mask);
}

void *Video_Memory_Alloc(u32 size)
{
	Video_MemoryBlock_t *block;
	Video_MemoryBlock_t *next;
	u32 aligned_size;
	u32 block_offset;
	u32 next_offset;
	u32 interrupt_mask;
	u32 consumed;

	if(size == 0U || size > (VIDEO_MEMORY_POOL_SIZE - VIDEO_MEMORY_HEADER_SIZE))
	{
		return NULL;
	}
	aligned_size = VIDEO_MEMORY_ALIGN(size);
	interrupt_mask = video_memory_lock();
	if(s_video_memory_initialized == 0U)
	{
		video_memory_initialize_locked();
	}

	block_offset = 0U;
	while(block_offset != VIDEO_MEMORY_OFFSET_NONE)
	{
		block = video_memory_block(block_offset);
		if(block->magic != VIDEO_MEMORY_BLOCK_MAGIC)
		{
			break;
		}
		if(block->state == VIDEO_MEMORY_BLOCK_FREE && block->size >= aligned_size)
		{
			if((block->size - aligned_size) >=
			   (VIDEO_MEMORY_HEADER_SIZE + VIDEO_MEMORY_ALIGNMENT))
			{
				next_offset = block_offset + VIDEO_MEMORY_HEADER_SIZE + aligned_size;
				next = video_memory_block(next_offset);
				next->magic = VIDEO_MEMORY_BLOCK_MAGIC;
				next->size = block->size - aligned_size - VIDEO_MEMORY_HEADER_SIZE;
				next->next_offset = block->next_offset;
				next->previous_offset = block_offset;
				next->state = VIDEO_MEMORY_BLOCK_FREE;
				memset(next->reserved, 0, sizeof(next->reserved));
				if(next->next_offset != VIDEO_MEMORY_OFFSET_NONE)
				{
					video_memory_block(next->next_offset)->previous_offset = next_offset;
				}
				block->next_offset = next_offset;
				block->size = aligned_size;
			}
			block->state = VIDEO_MEMORY_BLOCK_ALLOCATED;
			consumed = VIDEO_MEMORY_HEADER_SIZE + block->size;
			s_video_memory_current += consumed;
			if(s_video_memory_current > s_video_memory_peak)
			{
				s_video_memory_peak = s_video_memory_current;
			}
			video_memory_unlock(interrupt_mask);
			return (void *)&s_video_memory_pool[block_offset + VIDEO_MEMORY_HEADER_SIZE];
		}
		block_offset = block->next_offset;
	}

	s_video_memory_failures++;
	video_memory_unlock(interrupt_mask);
	return NULL;
}

void Video_Memory_Free(void *pointer)
{
	Video_MemoryBlock_t *block;
	Video_MemoryBlock_t *next;
	Video_MemoryBlock_t *previous;
	u32 block_offset;
	u32 released;
	u32 interrupt_mask;

	if(Video_Memory_Owns(pointer) == 0U)
	{
		return;
	}
	block_offset = (u32)((u8 *)pointer - s_video_memory_pool) -
		VIDEO_MEMORY_HEADER_SIZE;
	if((block_offset & (VIDEO_MEMORY_ALIGNMENT - 1U)) != 0U)
	{
		return;
	}

	interrupt_mask = video_memory_lock();
	block = video_memory_block(block_offset);
	if(block->magic != VIDEO_MEMORY_BLOCK_MAGIC ||
	   block->state != VIDEO_MEMORY_BLOCK_ALLOCATED)
	{
		video_memory_unlock(interrupt_mask);
		return;
	}

	released = VIDEO_MEMORY_HEADER_SIZE + block->size;
	block->state = VIDEO_MEMORY_BLOCK_FREE;
	if(s_video_memory_current >= released)
	{
		s_video_memory_current -= released;
	}
	else
	{
		s_video_memory_current = 0U;
	}

	if(block->next_offset != VIDEO_MEMORY_OFFSET_NONE)
	{
		next = video_memory_block(block->next_offset);
		if(next->magic == VIDEO_MEMORY_BLOCK_MAGIC &&
		   next->state == VIDEO_MEMORY_BLOCK_FREE)
		{
			block->size += VIDEO_MEMORY_HEADER_SIZE + next->size;
			block->next_offset = next->next_offset;
			if(block->next_offset != VIDEO_MEMORY_OFFSET_NONE)
			{
				video_memory_block(block->next_offset)->previous_offset = block_offset;
			}
			memset(next, 0, sizeof(*next));
		}
	}

	if(block->previous_offset != VIDEO_MEMORY_OFFSET_NONE)
	{
		previous = video_memory_block(block->previous_offset);
		if(previous->magic == VIDEO_MEMORY_BLOCK_MAGIC &&
		   previous->state == VIDEO_MEMORY_BLOCK_FREE)
		{
			previous->size += VIDEO_MEMORY_HEADER_SIZE + block->size;
			previous->next_offset = block->next_offset;
			if(previous->next_offset != VIDEO_MEMORY_OFFSET_NONE)
			{
				video_memory_block(previous->next_offset)->previous_offset =
					block->previous_offset;
			}
			memset(block, 0, sizeof(*block));
		}
	}
	video_memory_unlock(interrupt_mask);
}

void Video_Memory_ResetStats(void)
{
	u32 interrupt_mask = video_memory_lock();

	if(s_video_memory_initialized == 0U)
	{
		video_memory_initialize_locked();
	}
	s_video_memory_peak = s_video_memory_current;
	s_video_memory_failures = 0U;
	video_memory_unlock(interrupt_mask);
}

void Video_Memory_GetStats(Video_MemoryStats_t *stats)
{
	u32 interrupt_mask;

	if(stats == NULL)
	{
		return;
	}
	interrupt_mask = video_memory_lock();
	if(s_video_memory_initialized == 0U)
	{
		video_memory_initialize_locked();
	}
	stats->pool_size = VIDEO_MEMORY_POOL_SIZE;
	stats->current_bytes = s_video_memory_current;
	stats->peak_bytes = s_video_memory_peak;
	stats->largest_free_block = video_memory_largest_free_locked();
	stats->allocation_failures = s_video_memory_failures;
	video_memory_unlock(interrupt_mask);
}

u8 Video_Memory_Owns(const void *pointer)
{
	u32 address = (u32)pointer;
	u32 pool_start = (u32)&s_video_memory_pool[VIDEO_MEMORY_HEADER_SIZE];
	u32 pool_end = (u32)&s_video_memory_pool[VIDEO_MEMORY_POOL_SIZE];

	return (pointer != NULL && address >= pool_start && address < pool_end) ? 1U : 0U;
}

static u32 video_memory_lock(void)
{
	u32 interrupt_mask = __get_PRIMASK();

	__disable_irq();
	__DMB();
	return interrupt_mask;
}

static void video_memory_unlock(u32 interrupt_mask)
{
	__DMB();
	if(interrupt_mask == 0U)
	{
		__enable_irq();
	}
}

static Video_MemoryBlock_t *video_memory_block(u32 offset)
{
	return (Video_MemoryBlock_t *)&s_video_memory_pool[offset];
}

static void video_memory_initialize_locked(void)
{
	Video_MemoryBlock_t *block = video_memory_block(0U);

	block->magic = VIDEO_MEMORY_BLOCK_MAGIC;
	block->size = VIDEO_MEMORY_POOL_SIZE - VIDEO_MEMORY_HEADER_SIZE;
	block->next_offset = VIDEO_MEMORY_OFFSET_NONE;
	block->previous_offset = VIDEO_MEMORY_OFFSET_NONE;
	block->state = VIDEO_MEMORY_BLOCK_FREE;
	memset(block->reserved, 0, sizeof(block->reserved));
	s_video_memory_current = 0U;
	s_video_memory_peak = 0U;
	s_video_memory_failures = 0U;
	s_video_memory_initialized = 1U;
}

static u32 video_memory_largest_free_locked(void)
{
	Video_MemoryBlock_t *block;
	u32 block_offset = 0U;
	u32 largest = 0U;

	while(block_offset != VIDEO_MEMORY_OFFSET_NONE)
	{
		block = video_memory_block(block_offset);
		if(block->magic != VIDEO_MEMORY_BLOCK_MAGIC)
		{
			break;
		}
		if(block->state == VIDEO_MEMORY_BLOCK_FREE && block->size > largest)
		{
			largest = block->size;
		}
		block_offset = block->next_offset;
	}
	return largest;
}
