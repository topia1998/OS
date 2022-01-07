#ifndef FRAME_HEADER
#define FRAME_HEADER

#include "page.h"
#include "threads/palloc.h"

struct frame_e{
	struct list_elem elem;
	struct thread *t;
	void *kaddr;
	struct spt_e *spte;
};

void init_frame_list(void);

void insert_frame_e(struct list_elem *e);

struct frame_e* free_frame();

void add_frame_e(struct spt_e* spte,void *kaddr);
struct frame_e* clock_next();

void* frame_allocate(void* upage,enum palloc_flags flags);

void frame_free(void*);
void frame_free_without_palloc(void* kpage);
#endif
