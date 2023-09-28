#include <math.h>
#include <stdio.h>
#include "buddy.h"
#include "slab.h"
#include "helper.h"

typedef struct buddy_allocator {
	size_t size;	//array is actually size + 1
	size_t blocks;
	char* space;
	buddy_node** array;
} buddy_allocator;

buddy_allocator buddy;

typedef struct avail_list {
	buddy_node* array;
	size_t num_nodes;
	buddy_node* head;
} avail_list;

avail_list avail;

/*TESTING FUNCTIONS*/

void print_buddy_array() {
	for (int i = 0; i <= buddy.size; i++) {
		buddy_node* curr = buddy.array[i];
		printf("2^%d List: ", i);
		while (curr != NULL) {
			printf("*%d* -> ", curr->free_block);
			curr = curr->next;
		}
		printf("X\n");
	}
}

/*END OF TESTING FUNCTIONS*/

size_t get_pair(size_t block, size_t s) {
	if ((block / s) % 2 == 0)
		return block + s;
	else
		return block - s;
}

size_t min_u(size_t a, size_t b) {
	if (a > b)
		return b;
	else
		return a;
}

void free_node(buddy_node* old) {
	old->next = avail.head;
	avail.head = old;
}

buddy_node* allocate_node() {
	buddy_node* node = avail.head;
	avail.head = node->next;
	node->next = NULL;
	return node;
}

void add_node(size_t ind, buddy_node* node) {	//ind is the index of a list in the array
	node->next = buddy.array[ind];
	buddy.array[ind] = node;
}

buddy_node* remove_first_node(size_t ind) {
	if (buddy.array[ind] == NULL)
		return NULL;

	buddy_node* node = buddy.array[ind];
	buddy.array[ind] = node->next;
	node->next = NULL;
	return node;
}

buddy_node* remove_node(size_t ind, buddy_node* prev) {
	if (buddy.array[ind] == NULL)
		return NULL;

	buddy_node* node;
	if (prev == NULL) {
		node = buddy.array[ind];
		buddy.array[ind] = buddy.array[ind]->next;
	}
	else {
		node = prev->next;
		prev->next = node->next;
	}
	node->next = NULL;
	return node;
}

void buddy_init(void* space, size_t block_num) {
	buddy.space = (char*)space;

	buddy.size = highest_active_bit(block_num);
	buddy.blocks = 1U << buddy.size;

	if (buddy.blocks == block_num) {
		buddy.size--;
		buddy.blocks >>= 1;
	}
	avail.num_nodes = buddy.blocks >> 1;

	buddy.array = (buddy_node**) (&buddy.space[BLOCK_SIZE * buddy.blocks]);
	for (size_t i = 0; i < buddy.size; i++)
		buddy.array[i] = NULL;
		
	avail.array = (buddy_node*) (&buddy.array[buddy.size + 1]);
	for (size_t i = 1; i < avail.num_nodes - 1; i++)
		avail.array[i].next = &avail.array[i + 1];
	avail.array[avail.num_nodes - 1].next = NULL;
	avail.head = &avail.array[1];

	avail.array[0].free_block = 0; avail.array[0].next = NULL;
	buddy.array[buddy.size] = &avail.array[0];

}

void* buddy_alloc(size_t s) {
	size_t begin = highest_active_bit(s);
	size_t block;

	if (buddy.array[begin] != NULL) {
		block = buddy.array[begin]->free_block;
		free_node(remove_first_node(begin));

		return (void*)(&buddy.space[block * BLOCK_SIZE]);
	}

	size_t end = begin + 1;
	while (buddy.array[end] == NULL && end <= buddy.size) end++;

	if (end > buddy.size)	//out of memory
		return NULL; 

	block = buddy.array[end]->free_block;
	free_node(remove_first_node(end));
//	printf("first removal\n");
//	print_buddy_array();

	for (size_t i = end; i > begin; i--) {
		buddy_node* node = allocate_node();
		node->free_block = block + (1 << (i - 1));
		add_node(i - 1, node);
	//	printf("%i iteration\n", i);
	//	print_buddy_array();
	}

	return (void*)(&buddy.space[block * BLOCK_SIZE]);
}

void buddy_free(void* objp, size_t s) {
	char* objp_byte = (char*)objp;
	size_t block = (objp_byte - buddy.space) / BLOCK_SIZE;
	size_t entry = highest_active_bit(s);

	while (entry <= buddy.size) {
		buddy_node* node;
		if (buddy.array[entry] == NULL) {	//doesn't have a pair defintely
			node = allocate_node();
			node->free_block = block;
			add_node(entry, node);
			return;
		}

		size_t block_pair = get_pair(block, s);

		buddy_node* prev, * curr;
		for (prev = NULL, curr = buddy.array[entry]; curr != NULL; prev = curr, curr = curr->next) {
			if (block_pair == curr->free_block) {
				remove_node(entry, prev);	//curr is pointing to a non NULL value
				break;
			}
		}
		if (curr == NULL) {	//node's pair doesn't exist
			node = allocate_node();
			node->free_block = block;
			add_node(entry, node);
			return;
		}
		else {	//curr is node's pair
			block = min_u(block, block_pair);
			entry++;
			s *= 2;
			free_node(curr);
		}
	}
	
	
}