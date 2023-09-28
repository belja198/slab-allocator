#include <stdio.h>
#include <windows.h>
#include "slab.h"
#include "buddy.h"
#include "helper.h"

#define FREE_END 4096
#define SIZE_N_OFF 5

CRITICAL_SECTION CriticalSection;

typedef struct slab_s {
	unsigned int inuse;
	void* mem;	//including colour offset
	unsigned long colour_off;
	unsigned int free;
	unsigned int *free_array;
	
	struct slab_s* next;
} slab_t;

typedef struct cache_size_s {
	size_t cs_size;
	kmem_cache_t* cs_cachep;
} cache_size_t;

cache_size_t cache_sizes[13];

typedef struct kmem_cache_s {
	char name[20];
	size_t obj_size;	//bytes
	void (*ctor)(void*);
	void (*dtor)(void*);
	slab_t* empty;
	slab_t* full;
	slab_t* partial;
	size_t slab_num;
	size_t slab_size;	//blocks
	size_t obj_num_on_slab;
	unsigned wastage;
	size_t	colour;
	unsigned int colour_off;
	unsigned int colour_next;
	int growing;

	struct kmem_cache_s* next;
} kmem_cache_t;

kmem_cache_t cache_cache;
kmem_cache_t* tail_cache = &cache_cache;

double calc_cache_usage(kmem_cache_t* cachep) {
	int total_objects = cachep->slab_num * cachep->obj_num_on_slab;
	int total_allocated_objects = 0;
	slab_t* slab = cachep->full;
	while (slab) {
		total_allocated_objects += cachep->obj_num_on_slab;
		slab = slab->next;
	}
	slab = cachep->partial;
	while (slab) {
		total_allocated_objects += slab->inuse;
		slab = slab->next;
	}
	return (total_allocated_objects * 1.0 / total_objects) * 100;
}

int calc_num_of_blocks(size_t size) {
	size_t blocks = BLOCK_SIZE;
	size_t waste = blocks % size;

	while (waste > BLOCK_SIZE / 8) {
		blocks <<= 1;
		waste = blocks % size;
	}
	if (blocks % BLOCK_SIZE == 0)
		blocks <<= 1;
	return blocks / BLOCK_SIZE;
}

int calc_objs_on_slab(size_t obj_size, size_t slab_size) {
	size_t total = slab_size * BLOCK_SIZE - sizeof(slab_t);
	int num_object = 0;
	while (total >= sizeof(unsigned) + obj_size) {
		total -= (sizeof(unsigned) + obj_size);
		num_object++;
	}
	return num_object;
}

void init_slab(kmem_cache_t* cache, slab_t* slab) {
	slab->colour_off = cache->colour_next * cache->colour_off;
	cache->colour_next++;
	if (cache->colour_next == cache->colour - 1) cache->colour_next = 0;

	slab->mem = (void*)((unsigned long)slab + sizeof(slab_t) + cache->obj_num_on_slab * sizeof(unsigned) + slab->colour_off);
	slab->inuse = 0;
	slab-> next = 0;

	slab->free = 0;
	slab->free_array = (unsigned int*)((unsigned long int)slab + sizeof(slab_t));

	for (unsigned i = 0; i < cache->obj_num_on_slab - 1; i++) {
		slab->free_array[i] = i + 1;
	}
	slab->free_array[cache->obj_num_on_slab - 1] = FREE_END;

	if (cache->ctor) {
		void* curr_slot = slab->mem;
		for (unsigned i = 0; i < cache->obj_num_on_slab; i++) {
			(*cache->ctor)(curr_slot);
			curr_slot = (void*)((unsigned long)curr_slot + cache->obj_size);
		}
	}
}

void init_cache(kmem_cache_t* cache, const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*)) {
	snprintf(cache->name, 20, "%s", name);
	cache->obj_size = size;
	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->slab_size = calc_num_of_blocks(size);
	cache->obj_num_on_slab = calc_objs_on_slab(size, cache->slab_size);
	cache->wastage = cache->slab_size * BLOCK_SIZE - sizeof(slab_t) - cache->obj_num_on_slab * (cache->obj_size + sizeof(unsigned));
	cache->colour = cache->wastage / CACHE_L1_LINE_SIZE;
	cache->colour_off = CACHE_L1_LINE_SIZE;
	cache->colour_next = 0;
	cache->growing = 0;
	cache->full = 0; cache->partial = 0;
	cache->empty = buddy_alloc(cache->slab_size);
	init_slab(cache, cache->empty);
	cache->slab_num = 1;

	cache->next = 0;
}

void init_cache_sizes() {
	size_t size = 32;
	for (int i = 0; i < 13; i++) {
		cache_sizes[i].cs_size = size;
		cache_sizes[i].cs_cachep = 0;
		size <<= 1;
	}
}

void kmem_init(void* space, int block_num) {
	if (space == 0 || block_num == 0)
		exit(1);

	buddy_init(space, block_num);
	init_cache(&cache_cache, "kmem_cache", sizeof(kmem_cache_t), 0, 0);
	init_cache_sizes();

	if (!InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400)) {
		printf("Error: initializing critical section.\n"); exit(-1);
	}
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*)) {	// Allocate cache
	EnterCriticalSection(&CriticalSection);
	kmem_cache_t* cache = kmem_cache_alloc(&cache_cache);
	init_cache(cache, name, size, ctor, dtor);
	tail_cache->next = cache;
	tail_cache = cache;
	LeaveCriticalSection(&CriticalSection);
	return cache;
}

int kmem_cache_shrink(kmem_cache_t* cachep) {
	if (!cachep || cachep->growing || !cachep->empty)
		return 0;
	EnterCriticalSection(&CriticalSection);
	int num_blocks = 0;
	slab_t* curr = cachep->empty, *next;
	for (slab_t* curr = cachep->empty; curr; curr = next) {
		next = curr->next;
		buddy_free((void*)curr, cachep->slab_size);
		cachep->slab_num--;
		num_blocks++;
	}
	cachep->empty = 0;
	cachep->growing = 0;
	LeaveCriticalSection(&CriticalSection);
	return num_blocks;
}

void* kmem_cache_alloc(kmem_cache_t* cachep) {
	if (!cachep)
		return 0;
	EnterCriticalSection(&CriticalSection);
	slab_t* slab;
	if (cachep->partial) slab = cachep->partial;
	else if (cachep->empty) {
		slab = cachep->empty;
		slab->next = cachep->partial;
		cachep->partial = slab;
		cachep->empty = cachep->empty->next;
	}
	else {	//allocate partial
		cachep->partial = buddy_alloc(cachep->slab_size);
		init_slab(cachep, cachep->partial);
		cachep->slab_num++;
		slab = cachep->partial;
		cachep->growing = 1;
	}

	void* obj = (void*)((unsigned long)slab->mem + slab->free * cachep->obj_size);
	slab->free = slab->free_array[slab->free];
	slab->inuse++;

	if (slab->free == FREE_END) {
		cachep->partial = cachep->partial->next;
		slab->next = cachep->full;
		cachep->full = slab;
	}
	LeaveCriticalSection(&CriticalSection);
	return obj;
}

slab_t* find_slab_with_obj(kmem_cache_t* cache, slab_t* curr, void* objp) {
	while (curr) {
		if (objp >= curr->mem && objp < (void*)((unsigned long)curr + cache->slab_size * BLOCK_SIZE)) {
			return curr;
		}
		curr = curr->next;
	}
	return 0;
}

void free_obj(slab_t* slab, int ind) {	
	slab->free_array[ind] = slab->free;
	slab->free = ind;
	slab->inuse--;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
	if (!cachep || !objp)
		return;
	EnterCriticalSection(&CriticalSection);
	slab_t* slab = 0;
	int partial_flag = 0;
	if (cachep->full)
		slab = find_slab_with_obj(cachep, cachep->full, objp);
	if (cachep->partial && !slab) {
		slab = find_slab_with_obj(cachep, cachep->partial, objp);
		partial_flag = 1;
	}
	if (!slab) { 
		LeaveCriticalSection(&CriticalSection);
		return;
	} //error

	int ind = ((unsigned long)objp - (unsigned long)slab->mem) / cachep->obj_size;
	free_obj(slab, ind);

	if (cachep->dtor)
		(*(cachep->dtor))(objp);

	if (!partial_flag) {
		if (cachep->full == slab) 
			cachep->full = cachep->full->next;
		else {
			slab_t* prev;
			for (prev = cachep->full; slab != prev->next; prev = prev->next);
			prev->next = slab->next;
		}
		slab->next = cachep->partial;
		cachep->partial = slab;
	}
	else {
		if (cachep->partial == slab)
			cachep->partial = cachep->partial->next;
		else {
			slab_t* prev;
			for (prev = cachep->partial; slab != prev->next; prev = prev->next);
			prev->next = slab->next;
		}
		slab->next = cachep->empty;
		cachep->empty = slab;
	}
	LeaveCriticalSection(&CriticalSection);
}

void dealloc_slab(size_t slab_size, slab_t* slab) {
	while (slab) {
		slab_t* next = slab->next;
		buddy_free(slab, slab_size);
		slab = next;
	}
}

void kmem_cache_destroy(kmem_cache_t* cachep) {
	if (!cachep)
		return;
	EnterCriticalSection(&CriticalSection);
	kmem_cache_t* prev = &cache_cache, * curr = prev->next;
	while (curr) {
		if (curr == cachep)
			break;
		prev = curr; curr = curr->next;
	}
	if (!curr) {
		LeaveCriticalSection(&CriticalSection);
		return;
	}
	prev->next = curr->next;
	if (prev->next)
		tail_cache = prev;

	size_t slab_size = cachep->slab_size;
	if (cachep->empty) dealloc_slab(slab_size, cachep->empty);
	if (cachep->partial) dealloc_slab(slab_size, cachep->partial);
	if (cachep->full) dealloc_slab(slab_size, cachep->full);

	kmem_cache_free(&cache_cache, cachep);
	LeaveCriticalSection(&CriticalSection);
}

void* kmalloc(size_t size) {
	if (!size)
		return 0;

	size = nearest_power_of_two(size);
	int index = highest_active_bit(size);
	EnterCriticalSection(&CriticalSection);
	if (!cache_sizes[index - SIZE_N_OFF].cs_cachep) {
		char name[20];
		sprintf_s(name, 20, "size-%u", size);
		cache_sizes[index - SIZE_N_OFF].cs_cachep = kmem_cache_create(name, size, 0, 0);
	}
	LeaveCriticalSection(&CriticalSection);
	return kmem_cache_alloc(cache_sizes[index - SIZE_N_OFF].cs_cachep);
}

void kfree(const void* objp) {
	if (!objp)
		return;
	EnterCriticalSection(&CriticalSection);
	slab_t* slab = 0;
	int i;
	for (i = 0; i < 13; i++) {
		if (cache_sizes[i].cs_cachep) {
			if (cache_sizes[i].cs_cachep->full) {
				slab = find_slab_with_obj(cache_sizes[i].cs_cachep, cache_sizes[i].cs_cachep->full, objp);
				if (slab) break;
			}
			if (cache_sizes[i].cs_cachep->partial) {
				slab = find_slab_with_obj(cache_sizes[i].cs_cachep, cache_sizes[i].cs_cachep->partial, objp);
				if (slab) break;
			}
		}
	}

	kmem_cache_free(cache_sizes[i].cs_cachep, objp);
	LeaveCriticalSection(&CriticalSection);
}

void kmem_cache_info(kmem_cache_t* cachep) {
	EnterCriticalSection(&CriticalSection);
	printf("****CACHE INFO****\n");
	printf("name: %s\n", cachep->name);
//	printf("cache address: %p\n", cachep);
	printf("object size: %uB\n", cachep->obj_size);
	printf("cache size: %uB\n", cachep->slab_num * cachep->slab_size * BLOCK_SIZE);
	printf("slab num: %d\n", cachep->slab_num);
	printf("num objects/slab: %d\n", cachep->obj_num_on_slab);
	double usage = calc_cache_usage(cachep);
	printf("cache usage: %.3lf%% \n", usage);
	printf("-----------------\n");
	LeaveCriticalSection(&CriticalSection);
}