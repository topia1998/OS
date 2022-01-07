#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void user_vaddr(const void *vaddr){
	if(!is_user_vaddr(vaddr))
		sys_exit(-1);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	uint32_t syscall_num;
  	syscall_num = *(uint32_t *)f->esp;
	switch(*(uint32_t*)(f->esp)){
		case SYS_HALT:
			sys_halt();
			break;
		case SYS_EXIT:
			user_vaddr(f->esp+4);
			sys_exit(*(uint32_t*)(f->esp+4));
			break;
		case SYS_EXEC :
			user_vaddr(f->esp+4);
			f->eax = sys_exec((char*)*(uint32_t*)(f->esp+4));
			break;
		case SYS_WAIT :
			user_vaddr(f->esp+4);
			f->eax = sys_wait(*(uint32_t*)(f->esp+4));
			break;
		case SYS_WRITE:
			f->eax = sys_write((int)*(uint32_t*)(f->esp+4), (const void*)*(uint32_t*)(f->esp+8),
					(unsigned)*(uint32_t*)(f->esp+12));
			break;
		case SYS_FIB:
			f->eax=sys_fibonacci((int)*(uint32_t*)(f->esp+4));
			break;
		case SYS_MAX:
			f->eax=sys_max_of_four_int((int)*(uint32_t*)(f->esp+4), (int)*(uint32_t*)(f->esp+8),
					(int)*(uint32_t*)(f->esp+12), (int)*(uint32_t*)(f->esp+16));
			break;
   } 

	return;

  //thread_exit ();
}

void sys_halt(void){
	shutdown_power_off();
}

void sys_exit (int status){
  	printf("%s: exit(%d)\n", thread_name(), status);
	thread_current()->exit_status = status;
	thread_exit();
}

pid_t sys_exec (const char *file){
	return process_execute(file);
}

int sys_wait (pid_t pid){
	return process_wait(pid);
}

int sys_read (int fd, void *buffer, unsigned length){
	int i;
	if(fd==0){
		for(i=0; i<(int)length; i++){
			uint8_t c = input_getc();
			if(c=='\0')
				break;
		}
		return i;
	}
	else
		return -1;
}

int sys_write (int fd, const void *buffer, unsigned length){
	if(fd==1){
		putbuf((char*)buffer, (size_t)length);
		return length;
	}
	else 
		return -1;
}

int sys_fibonacci (int n){
	return n < 3 ? 1 : sys_fibonacci(n - 1) + sys_fibonacci(n - 2);
}

int sys_max_of_four_int(int a, int b, int c, int d){
	int max;
	max = (a > b) ? a : b;
	max = (max > c) ? max : c;
	max = (max > d) ? max : d;
	return max;
}
