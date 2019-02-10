#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct mem_block
{
	struct mem_block* next;
	struct mem_block* prev;
	unsigned char free;
};

struct mem_block* mem_root = 0;
struct mem_block* last_block = 0;
struct mem_block* first_free_block = 0;
unsigned char free_exist = 0;

#define BLOCK_DATA(b)     ((b) + 1)
#define BLOCK_HEADER(ptr) ((struct mem_block*)(ptr) - 1)

size_t get_block_size(struct mem_block* curr_block)
{
	struct mem_block* end_of_block = (struct mem_block*)sbrk(0);
	size_t curr_block_size = 0;

	if (curr_block == last_block)
	{
		curr_block_size = (unsigned long)end_of_block - (unsigned long)curr_block - sizeof(struct mem_block);
	}
	else
	{
		curr_block_size = (unsigned long)curr_block->next - (unsigned long)curr_block - sizeof(struct mem_block);
	}

	return curr_block_size;
}

void give_back_memory()
{
	struct mem_block* program_break = (struct mem_block*)sbrk(0);
	unsigned long block_last_addr;
	size_t last_block_size;

	if (!program_break || !last_block)
	{
		return;
	}
	/* Address of the last block in the list */
	block_last_addr = (unsigned long)last_block;

	last_block_size = get_block_size(last_block);
	/* If is is free block and matches the program break - it can be released */
	if (last_block->free && block_last_addr + last_block_size == (unsigned long)program_break - sizeof(struct mem_block))
	{
		struct mem_block* prev_block = last_block->prev;
		int remove_root = 0;
		int remove_free = 0;

		if (last_block == mem_root)
		{
			remove_root = 1;
		}
		if (first_free_block == last_block)
		{
			remove_free = 1;
		}
		if (brk(last_block) == 0) /* Returned to the OS */
		{
			if (remove_root)
			{
				mem_root = 0;
				last_block = 0;
				first_free_block = 0;
			}
			else
			{
				prev_block->next = 0;
				last_block = prev_block;
			}
			if (remove_free)
			{
				first_free_block = 0;
			}
			free_exist--;
		}
	}
}

struct mem_block* find_free_block(size_t size)
{
	struct mem_block* curr_block = mem_root;

	if (free_exist == 0)
	{
		return 0;
	}

	if (first_free_block)
	{
		curr_block = first_free_block;
	}

	/* Get the first fit block */
	while (curr_block)
	{
		size_t curr_block_size = get_block_size(curr_block);

		if (curr_block->free && !first_free_block)
		{
			first_free_block = curr_block;
		}

		if (curr_block->free && curr_block_size >= size)
		{
			break;
		}
		curr_block = curr_block->next;
	}

	return curr_block;
}

struct mem_block* get_more_memory(size_t size)
{
	/* Batching allocs */
	size_t alloc_size = (size >= 64 ? size + sizeof(struct mem_block) : 32*(size + sizeof(struct mem_block)));
	struct mem_block* new_block = (struct mem_block*)sbrk(alloc_size);

	/* OS allocation failed */
	if (!new_block)
	{
		return 0;
	}

	/* Update FreeList if not set */
	if (!mem_root)
	{
		mem_root = new_block;
		mem_root->next = 0;
		mem_root->prev = 0;
		mem_root->free = 0;
		new_block = mem_root;
		last_block = mem_root;
	}
	else
	{
		last_block->next = new_block;

		/* Update block metadata */
		new_block->next = 0;
		new_block->prev = last_block;
		new_block->free = 0;
		last_block = new_block;
	}
	return new_block;
}

void *mymalloc(size_t size)
{
	/* Safely return NULL on zero size */
	if (size == 0)
	{
		return 0;
	}

	/* Align to sizeof(long) */
	while (size % sizeof(long))
	{
		size++;
	}

	struct mem_block* next_block = find_free_block(size);

	/* Could not find free block, so request memory from system */
	if (!next_block)
	{
		next_block = get_more_memory(size);
	}
	else
	{
		free_exist--;
	}

	size_t curr_block_size = get_block_size(next_block);


	/* Split free block if possible */
	if (curr_block_size > size + sizeof(struct mem_block))
	{
		struct mem_block* splitted_block = (struct mem_block*)((unsigned long)next_block + size + sizeof(struct mem_block));
		splitted_block->free = 1;
		free_exist++;


		/* Splitted block size will be not include the sizes of current clock header and its own header */
		splitted_block->next = next_block->next;
		splitted_block->prev = next_block;
		if (next_block->next)
		{
			next_block->next->prev = splitted_block;
		}
		/* Splitted block header begins after current block header and size of memory*/
		next_block->next = splitted_block;

		if (last_block == next_block)
		{
			last_block = splitted_block;
		}
		if (!first_free_block || first_free_block == next_block)
		{
			first_free_block = splitted_block;
		}
	}

	/* Could not find free block or requset the new one, so just return NULL */
	if (!next_block)
	{
		return 0;
	}
	if (first_free_block == next_block)
	{
		first_free_block = 0;
	}

	/* Mark block allocated */
	next_block->free = 0;

	return BLOCK_DATA(next_block);
}

void *mycalloc(size_t nmemb, size_t size)
{
	void* return_ptr = mymalloc(nmemb * size);

	if (return_ptr)
	{
		struct mem_block* block_tmp = BLOCK_HEADER(return_ptr);
		size_t curr_block_size = get_block_size(block_tmp);
		unsigned int i = 0;
		/* Clean memory */
		for (i = 0; i < curr_block_size; i++)
		{
			*((char*)return_ptr + i) = '\0';
		}
	}

	return return_ptr;
}

static void unit_free_blocks(struct mem_block* curr_block)
{
	struct mem_block* block_left = curr_block;
	struct mem_block* block_right = curr_block->next;
	unsigned long block_left_addr, block_right_addr;


/* merge with right */
	block_left_addr = (unsigned long)block_left;
	if (block_right)
	{
		size_t left_block_size = get_block_size(block_left);

		block_right_addr = (unsigned long)block_right;
		if ((block_left_addr + left_block_size + sizeof(struct mem_block) == block_right_addr) && block_left->free && block_right->free)
		{
			if (last_block == block_right)
			{
				last_block = block_left;
			}
			/* Merge blocks */
			block_left->next = block_right->next;
			if (block_right->next)
			{
				block_right->next->prev = block_left;
			}
			free_exist--;
		}
	}
/*merge with left */
	if (curr_block != mem_root)
	{
		block_left = curr_block->prev;
		block_right = curr_block;

		block_left_addr = (unsigned long)block_left;
		block_right_addr = (unsigned long)block_right;

		size_t left_block_size = get_block_size(block_left);

		if ((block_left_addr + left_block_size + sizeof(struct mem_block) == block_right_addr) && block_left->free && block_right->free)
		{
			/* Merge blocks */
			block_left->next = block_right->next;
			if (block_right->next)
			{
				block_right->next->prev = block_left;
			}

			if (last_block == block_right)
			{
				last_block = block_left;
			}
			free_exist--;
		}
	}
}


void myfree(void* ptr)
{
	/*If ptr is NULL, no operation is performed.*/
	if (!ptr)
	{
		return;
	}
	/* No allocated memory */
	if (!mem_root)
	{
		return;
	}

	/* Get the block */
	struct mem_block* curr_block = BLOCK_HEADER(ptr);

	/* Check if block is present in the list. */
	struct mem_block* iter_block = mem_root;

	while (iter_block)
	{
		/* block is found */
		if (iter_block == curr_block)
		{
			break;
		}
		iter_block = iter_block->next;
	}
	/* Block not found - freeing not allocated memory */
	if (!iter_block)
	{
		return;
	}

	/* Block found but is already freed - double freeing */
	if (iter_block->free)
	{
		return;
	}

	iter_block->free = 1;
	if (!first_free_block || iter_block < first_free_block)
	{
		first_free_block = iter_block;
	}

	free_exist++;

	/* Unit free blocks if need */
	unit_free_blocks(iter_block);
	give_back_memory();
}

void *myrealloc(void *ptr, size_t size)
{
	/* If NULL passed - act like malloc */
	if (!ptr)
	{
		return mymalloc(size);
	}
	/* if zero size passed - act like free()*/
	else if (size == 0)
	{
		myfree(ptr);
		return 0;
	}

	struct mem_block* tmp_block = BLOCK_HEADER(ptr);

	size_t curr_block_size = get_block_size(tmp_block);

	/* Pick the smallest overlapping size */
	int new_size = (curr_block_size > size ? size : curr_block_size);

	/* If the sizes are equal, do not do anything */
	if (curr_block_size == size)
	{
		return ptr;
	}
	else if (curr_block_size > size + sizeof(struct mem_block) )/* alloc in current memory block */
	{
		/* Split block and return the right half to the heap */
		struct mem_block* splitted_block = (struct mem_block*)((unsigned long)tmp_block + size + sizeof(struct mem_block));

		splitted_block->free = 1;
		free_exist++;
		/* Splitted block size will be not include the sizes of current clock header and its own header */
		splitted_block->next = tmp_block->next;
		splitted_block->prev = tmp_block;
		if (tmp_block->next)
		{
			tmp_block->next->prev = splitted_block;
		}

		/* Splitted block header begins after current block header and size of memory*/
		tmp_block->next = splitted_block;

		if (last_block == tmp_block)
		{
			last_block = splitted_block;
		}
		if (!first_free_block || first_free_block == tmp_block)
		{
			first_free_block = splitted_block;
		}

		return BLOCK_DATA(tmp_block);
	}
	else
	{
		void* return_ptr = mymalloc(size);

		if (return_ptr)
		{
			int i = 0;
			/* Copy memory from the previous pointer */
			for (i = 0; i < new_size; i++)
			{
				*((char*)return_ptr+i) = *((char*)ptr+i);
			}
		}
		/* Release previous memory */
		myfree(ptr);
		return return_ptr;
	}
}


/*
 * Enable the code below to enable system allocator support for your allocator.
 */
void *malloc(size_t size) { return mymalloc(size); }
void *calloc(size_t nmemb, size_t size) { return mycalloc(nmemb, size); }
void *realloc(void *ptr, size_t size) { return myrealloc(ptr, size); }
void free(void *ptr) { myfree(ptr); }
