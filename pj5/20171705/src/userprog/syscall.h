#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void halt();
void exit(int exit_number);
int exec(const char* filename);
int wait(int pid);
int read(int fd,int* buffer,unsigned size);
int write(int fd,int *buffer,unsigned size);
int fibonacci(int n);
int max_of_four_int(int a,int b,int c,int d);
int create(const char* file, unsigned initial_size);
int remove(const char* file);
int open(const char* file);
int filesize(int fd);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

#endif /* userprog/syscall.h */
