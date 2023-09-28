#pragma once

#include <stdlib.h>

typedef struct buddy_node {
	size_t free_block;
	struct buddy_node* next;
} buddy_node;


void buddy_init(void* space, size_t block_num);

void* buddy_alloc(size_t s);	//allocate s blocks

void buddy_free(void* objp, size_t s);