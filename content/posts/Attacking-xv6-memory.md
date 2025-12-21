+++
title = "Attacking xv6 memory"
author = "vibhatsu"
tags =["xv6", "OS"]
cover = ""
coverCaption = "Always zero-out memory"
description = "An explained solution to xv6 syscall-lab's final task"
readingTime = true
comments = true
keywords = ["xv6", "OS", "memory"]
date = '2025-01-01T19:23:33+05:30'
+++
I have been reading and trying out MIT's course on xv6. In its syscall lab, the final task was very interesting. In this lab, pages were not getting zeroed out when allocated. So, there was chance of memory leak from the previous process. I was stuck on this task for a long time and could not find any help online. So, I read the code and the xv6 book and thought of a solution. Here's how it works.

## The task
The bug is that the call to memset(mem, 0, sz) at line 272 in kernel/vm.c to clear a newly-allocated page is omitted when compiling this lab. Similarly, when compiling kernel/kalloc.c for this lab the two lines that use memset to put garbage into free pages are omitted. The net effect of omitting these 3 lines (all marked by ifndef LAB_SYSCALL) is that newly allocated memory retains the contents from its previous use.

>user/secret.c writes an 8-byte secret in its memory and then exits (which frees its memory). Your goal is to add a few lines of code to user/attack.c to find the secret that a previous execution of secret.c wrote to memory, and write the 8 secret bytes to file descriptor 2. You'll receive full credit if attacktest prints: "OK: secret is ebb.ebb". (Note: the secret may be different for each run of attacktest.)
You are allowed to modify user/attack.c, but you cannot make any other changes: you cannot modify the xv6 kernel sources, secret.c, attacktest.c, etc. 

Here's the code for attacktest.c:
```c
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

char secret[8];
char output[64];

// from FreeBSD.
int
do_rand(unsigned long *ctx)
{
/*
 * Compute x = (7^5 * x) mod (2^31 - 1)
 * without overflowing 31 bits:
 *      (2^31 - 1) = 127773 * (7^5) + 2836
 * From "Random number generators: good ones are hard to find",
 * Park and Miller, Communications of the ACM, vol. 31, no. 10,
 * October 1988, p. 1195.
 */
    long hi, lo, x;

    /* Transform to [1, 0x7ffffffe] range. */
    x = (*ctx % 0x7ffffffe) + 1;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0)
        x += 0x7fffffff;
    /* Transform to [0, 0x7ffffffd] range. */
    x--;
    *ctx = x;
    return (x);
}

unsigned long rand_next = 1;

int
rand(void)
{
    return (do_rand(&rand_next));
}

// generate a random string of the indicated length.
char *
randstring(char *buf, int n)
{
  for(int i = 0; i < n-1; i++) {
    buf[i] = "./abcdef"[(rand() >> 7) % 8];
  }
  if(n > 0)
    buf[n-1] = '\0';
  return buf;
}

int
main(int argc, char *argv[])
{
  int pid;
  int fds[2];

  // an insecure way of generating a random string, because xv6
  // doesn't have good source of randomness.
  rand_next = uptime();
  randstring(secret, 8);
  
  if((pid = fork()) < 0) {
    printf("fork failed\n");
    exit(1);   
  }
  if(pid == 0) {
    char *newargv[] = { "secret", secret, 0 };
    exec(newargv[0], newargv);
    printf("exec %s failed\n", newargv[0]);
    exit(1);
  } else {
    wait(0);  // wait for secret to exit
    if(pipe(fds) < 0) {
      printf("pipe failed\n");
      exit(1);   
    }
    if((pid = fork()) < 0) {
      printf("fork failed\n");
      exit(1);   
    }
    if(pid == 0) {
      close(fds[0]);
      close(2);
      dup(fds[1]);
      char *newargv[] = { "attack", 0 };
      exec(newargv[0], newargv);
      printf("exec %s failed\n", newargv[0]);
      exit(1);
    } else {
       close(fds[1]);
      if(read(fds[0], output, 64) < 0) {
        printf("FAIL; read failed; no secret\n");
        exit(1);
      }
      if(strcmp(secret, output) == 0) {
        printf("OK: secret is %s\n", output);
      } else {
        printf("FAIL: no/incorrect secret\n");
      }
    }
  }
}
```

Let's go through the attacktest program step by step and understand each function in the order they called.
Firstly, it forks.
### Fork
Inside fork, it firsts calls `allocproc` function.
```c
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
```


`allocproc` then calls `kalloc` for trapframe. This allocates 1 page for the trapframe.

```c
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
```

`proc_pagetable` is called to create a new page table for the new process.

```c
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}
```

Inside this, we see another page being allocated for pagetable of the process. This allocates another new page for the root pagetable. 
After that, mapping is created for `TRAMPOLINE` and `TRAPFRAME` in the new page table. Since both these reside in the higher memory, their entries are present in the same third level pagetable.
Hence, 2 pages will be allocated for pagetable- 1 for 2nd level pagetable and 1 for 3rd level pagetable.

So, in all, 4 pages are allocated in `allocproc` function.(trapframe-1,pagetable-3 and trampoline is common for all so page was allocated for it but it was mapped into the address space of the new process)

After that, we see that `uvmcopy` is being called which copies parent's memory to child's memory(excluding trampoline and trapframe as `uvmcopy` is used). This includes the stack, text, data and heap of the parent process. Also, in xv6, there is a guard page between the stack and text section. In xv6, only 1 page is allocated for the stack and 1 page used as guard page without the `PTE_U` bit set so if the stack overflows to guard page, it raises exception. 
At this point, the parent has 4 pages allocated, so the child will also have these 4 pages+ 2 pages for 2nd and 3rd level pagetables(all are in lower memory and close enough to have entries in the same 3rd level pagetable).
```c
  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;
```
So, `uvmcopy` allocated in total 6 pages.

So, fork allocates 10 pages in total.

### Exec
After fork, exec is called. 
```c
int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;
```

The first allocation happens in `proc_pagetable` function. This is the same as the one we saw in fork. So, 3 pages are allocated here.

Then each segment inside program header is loaded in. This includes 2 load segments - data and text. Since these are in adjacent pages in memory, they will have entries in the same 3rd level pagetable. So, 2 pages are allocated for the pagetables and 2 for each of the segments. So, 4 pages in total.

```c
// Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;
```

Next memory is allocated for stack. In xv6, only 1 page is allocated for the stack and 1 guard page. Since these pages are just above the data segment, they will have entries in the same 3rd level pagetable. So, only 2 extra pages are allocated.
```c
 // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK+1)*PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-(USERSTACK+1)*PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK*PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;
```
At the end, we can see `proc_freepagetable` being called. This is called to free the pagetables created by fork. So, 4 pages of memory(stack+guard and data+text) are freed. And then the pagetables are freed. So, 5 pages are freed.
```c
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // this ends up in a0, the first argument to main(argc, argv)
```
So, in short, exec allocates 9 pages and then frees 9 pages. Although, this seems no overall change in memory, the pages allocated are different and so the free list is different.

## secret
```c
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"


int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf("Usage: secret the-secret\n");
    exit(1);
  }
  char *end = sbrk(PGSIZE*32);
  end = end + 9 * PGSIZE;
  strcpy(end, "my very very very secret pw is:   ");
  strcpy(end+32, argv[1]);
  exit(0);
}
```
The attacktest calls `exec` with `secret`. It allocates 32 pages and writes secret on 9th page(starting from 0). The attacktest then waits for the child to finish. The `wait` syscall frees the memory allocated by the child process. It calls `freeproc` on the child. It first frees the page for trapframe. It then calls `proc_freepagetable` to free memory allocated. It first frees text and data segment, followed by stack and guard page, 32 pages of heap. After this, it finally frees the pages containing the pagetables.

It is worth noting how xv6 maintains and uses the free memory. It maintains a free  linked list and newly freed pages are added to the head and pages are taken from the head for allocation. How it is done is the free page contains the physical address of the next free page. So, when a page is freed, the first 8 bytes of the page are overwritten with the address of the next free page. So, when a page is allocated, the first 8 bytes are read to get the address of the next free page. This is how the free list is maintained.

## attack
After the wait, it then again forks and exec with `attack`. From the above discussion, we know, fork will take 10 pages in total. These will be 5 pages that previously had the pagetables and 5 pages from the 32 pages allocated in heap(page 31-27). The exec will then allocate 9 pages(page 26-18). Then it frees 9 pages. So, now the freelist would be:
```
9 pages-> page 17 of heap-> ... -> page 9 of heap -> ... -> page 0 of heap
```
So, we would need to allocate 9+(17-9) that is 17 pages so that our secret containing page is allocated again. So, then we just need to read from 32 bytes from the 16th page(0 indexed) to get the secret.
```c
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)
  char *mem = sbrk(PGSIZE * 17);
  mem = mem + 16 * PGSIZE;
  write(2, mem + 32, 8);
  exit(1);
}
```
However, this attack would have failed if the secret was written at the start of a page as when freed, the 1st 8 bytes of a freed page are overwritten with the address of the next free page. So, the secret would have been overwritten.
