#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "page.h"
#include "threads/vaddr.h"
#define TRUE 1

void stack_growth (struct thread *c, const void* addr){
	void *k = palloc_get_page(PAL_USER);
	void *u = pg_round_down(addr);
	pagedir_get_page(c->pagedir, u);
	pagedir_set_page(c->pagedir, u, k, TRUE);
}
