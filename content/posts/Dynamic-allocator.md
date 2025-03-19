+++
date = '2025-03-16T18:58:51+05:30'
title = 'Dynamic Allocator'
author = 'vibhatsu'
tags = ["memory management", "dynamic allocator"]
cover = ""
coverCaption = ""
description = "This is the first blog in the series of dynamic allocators explaining the basic essence of dynamic memory allocation"
readingTime = true
keywords = ["OS", "dynamic allocator"]                          
+++

We all have been using dynamic allocators in our programs from time to time. However, we barely take any time to appreciate the beauty of dynamic allocation and the underlying mechanisms. In this blog, I will try to explain the basic idea of dynamic memory allocation and the underlying mechanisms. This is the first blog explaining the basic idea of dynamic memory allocation from a development view by exploring the idea of a simple dynamic allocator. In the upcoming blogs, I will share my experience of implementing a custom dynamic allocator that works on `Buddy System` algorithm.

## What is Dynamic Memory Allocation?
In programming, at certain points, we have felt the need of having memory whose size is not known at compile time. Hence, we need to allocate memory `dynamically`. This is the essence of dynamic memory allocation. In C, we use `malloc`, `calloc`, `realloc` and `free` functions to allocate and deallocate memory dynamically. In C++, we use `new` and `delete` operators for the same purpose.

There are 2 types of dynamic allocators:
1. **Explicit Allocators:** In this type of allocators, the programmer explicitly needs to free the memory he/she has allocated. An example is `malloc` in C and `new` in C++.
2. **Implicit Allocators:** In this type of allocators, the memory is automatically freed when it is no longer needed. An example is allocation in go which is garbage collected.

## Heap

The memory allocated dynamically is stored in a region called `heap`. Heap is shared among all the threads of a process. The top of the heap is called `brk`. The heap grows upwards and the stack grows downwards (For those who may not be familiar with memory structure, stack is the memory where local variables are stored for each function call).


### Why dynamic allocators?

At its fundamental level, all dynamic allocators end in a syscall on scarcity of memory called `sbrk`.
```c
    void *sbrk(intptr_t incr);
    // returns old brk value else -1 on error
```
This increases the memory of heap by `incr` bytes and returns the old `brk` value. This way, we can now have accesss to `incr` extra bytes of memory.
Now, we can directly allocate memory using raw `srbk` calls. However, it can result in too many syscalls and can be inefficient for memory management and as we know, syscalls can be very expensive.

## Constraints for dynamic allocator

Now, that we realize the importance of dynamic allocators, let's see what in brief what constraints we need to consider while designing a dynamic allocator:
- **Handling arbitary request sequences**: An application can make arbitary allocation and free requests - the only constraint being each free request must correspond to a previous allocation request.
- **Making immediate responses to requests**: The allocator should add any latency to the allocation and free requests. In other words, we cannot buffer the requests and process them later.
- **Using only heap**: For storing the information related to the allocated blocks, we can only use the heap. 
- **Aligning blocks**: The blocks allocated should be aligned to store any type of data.
- **Not modifying allocated blocks**:  Allocators should not modify the allocated blocks as it can lead to undefined behaviour. They can only manipulate the free blocks.

## Goals
We have basically 2 goals.
1. **Maximizing throughput**: number of requests processed per unit time
2. **Maximizing memory utilization**: If \(P_k\) is aggregate sum of all currently allocated blocks, \(H_k\) is the current heap size, then the allocator's task is to maximize peak memory utilization \(U_k\).
$$
U_k={\frac {max_{i \le k}(P_i)} {H_k}}
$$

Let's visualize this with an example:
Suppose we request memory of 4 bytes. Now, consider the following 2 scenarios:
1. The allocator allocates 8 bytes to the application.
2. The allocator allocates exactly 5 bytes to the application.
In the below images, the dark green blocks represent bytes that are actually used by the application and the light green blocks represent the bytes that are allocated but not used.
![poor-utilization](../images/Dyn-Alloc/Dyn-Alloc-poor-utilization.svg)
![better-utilization](../images/Dyn-Alloc/Dyn-Alloc-better-utilization.svg)
It is clear that in the second scenario, the memory utilization is better as the memory allocated is roughly equal to the memory used by the application.

## Fragmentation
The biggest enemy of memory utilization is fragmentation. It is basically described by the heap memory that is allocated but not actually used for anything. Consider the following example:

Suppose we have a heap of size 16 bytes. We have a dynamic allocator that uses 1 byte to store information about the allocated block( basically the size of the block). This byte is followed by the actual bytes allocated that will available to the application for its usage.
Now, suppose for the sake of alignment, every allocated block must have size in power of 2. So, if we request allocation of `5` bytes. So, the allocator will allocate `8` bytes to the application. As the application only needs `5` bytes, the remaining `3` bytes are wasted. This is called `internal fragmentation`.
![Internal-Fragmentaion](../images/Dyn-Alloc/Dyn-Alloc-internal-frag.svg)
Now, suppose we had made the following requests.
```c
...
a = get_memory(3);
b = get_memory(1);
c = get_memory(4);
d = get_memory(2);
e = get_memory(1);
...
```
After the above requests, the heap would look like the following:
![External-Fragmentaion](../images/Dyn-Alloc/Dyn-Alloc-external-frag-1.svg)
Now, suppose we free some blocks.
```c
...
free_memory(b);
free_memory(d);
...
```
The following would be the state of the heap after the above requests.
![External-Fragmentaion](../images/Dyn-Alloc/Dyn-Alloc-external-frag-2.svg)
Now, suppose we need to allocate 5 bytes. We can see that we have enough memory overall but we cannot allocate 5 bytes contiguously. This is called `external fragmentation`.

## Implemtation Issues

- _Free block organization:_ How free blocks will be handled? Whether we want to use a linked list for it or store it in some other way.
- _Placement:_ Which free block to use for allocation? Consider the scene where we have multiple scattered free blocks separated by allocated blocks. Now, there is a request for allocation. Which free particular free block should be used? Should we use the first free block? Should we use the smallest free block that can accomodate the request?
- _Splitting:_ what to do with remainder of the free block after allocation?
- _Coalescing:_ Combining adjacent free blocks. If we free a block next to an already free block, we need to combine them or else our algorithm to find the free block that suffices the request will fail to recognize the combined free block.

---
In this blog, we have seen the basic idea of dynamic memory allocation, its constraints and goals and some of the issues that are faced while implementing it. In the next blog, we will explore the idea of a simple allocator called `Implicit Free list` allocator and see how it deals with the goals and issues we have discussed in this blog.