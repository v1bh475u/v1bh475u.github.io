+++
date = '2026-07-14T12:07:07+05:30'
draft = true
title = 'A curious case of alternating good and bad allocation'
author = 'vibhatsu'
tags = ["cache-analysis", "linux-memory-management", "hugetlb", "huge-pages", "performance"]
cover = ""
coverCaption = ""
description = "This is a continuation of my journey to find the reason why the L3 regime decided to appear in some cases and not in others. Reference blog post [[here]](https://vibhatsu.me/posts/cache-walk)"
readingTime = true
comments = true
keywords = ["HugeTLB", "Linux buddy allocator", "explicit huge pages", "LLC", "cache latency", "PFN", "physical memory layout", "Intel Alder Lake", "cache associativity", "page allocation"]
+++

In the previous blog [here](https://vibhatsu.me/posts/cache-walk), I tried to replicate the textbook plot on my machine. I had to tweak a lot of knobs before finally finding the plot, but the cause of the plot was completely unexplained. In this post, I am going to continue my journey of analyzing why the plot did not appear in the first experiment itself!

## A Quick Recap

Last time, for the plot, I changed 3 conditions:

- Turned off the prefetchers;
- Did the experiment directly after boot; and
- Allocated a single chunk and used prefixes from it.

So, for starters, I focused on simpler and more tangible tests _(whispers: wrong move)_. I repeated the same experiment but with fresh allocations.

![Fresh allocation plot](../images/cache-digging/fresh_run_curves.svg)

Well, I was not expecting there to be much difference, except that since there is a fresh allocation per size in a fresh run, there has to be much more noise. As you may notice, there are multiple graphs: one graph per run, with 20 runs in total. I then decided to see how each size performed across the runs.

![Fresh degradation](../images/cache-digging/fresh_run_degradation.svg)

There seems to be an alternating pattern of good runs and bad runs. I decided to do the same for the same-backing one and got this:

![Same backing degradation](../images/cache-digging/same_run_degradation.svg)

It has the same alternating pattern but with a smaller magnitude! I re-ran it multiple times, and the same sort of alternating pattern occurred. This did not match my understanding at all!

## A tiny tour on how linux manages memory

First of all, I am going to abstract a **LOT** for the simplification of the explanation, so please take this with a grain of salt. Hopefully, this will help explain why I was so surprised.

### NUMA

First of all, at a high level, memory is divided into NUMA nodes. NUMA (Non-Uniform Memory Access) is a system in which some regions of memory are closer to one core than another, resulting in different memory access times. Thus, it is advantageous for processes to use NUMA nodes closer to the core on which they run.

On my system, there is only one NUMA node, so it makes no difference to my observations.

Linux then divides each node into `zones`.

### Zones

Why zones? Because certain requirements need to be fulfilled:

- `ZONE_DMA`: very low memory for legacy DMA devices.
- `ZONE_DMA32`: memory below 4 GiB for devices with 32-bit addressing.
- `ZONE_NORMAL`: ordinary directly mapped RAM on x86-64.
- `ZONE_MOVABLE`: pages intended to remain movable for migration/hotplug.
- `ZONE_HIGHMEM`: mainly relevant to 32-bit systems, not normal x86-64.

Each zone has a buddy allocator handling memory requests.

### Buddy allocator

It allocates memory with a normal page (`4 KiB`) as the smallest unit.

A buddy allocator has multiple freelists, each containing chunks whose sizes are powers of 2, as shown below:

<!-- image of different orders -->
Each list contains only those chunks that satisfy the size requirement.

The core idea behind the buddy allocator is that it divides memory into 2 buddies. Let's take an example.

Let's say we have our `4 GiB` memory and the highest order of our buddy allocator is `11`, i.e., the highest order freelist contains chunks of size \(2^{11 - 1}\ *\ 4\text{ KiB} = 4\text{ MiB} \).

Hence, the `4 GiB` memory is initially divided into multiple chunks of `4 MiB` each. There will be \( 2^{12}\) MiB / 4 MiB = 1024 chunks in the highest order. (Note: I am not considering the DMA32 zone reservation or any other reservation for this explanation.)

With this initial setup, let's say our allocator gets a request for the lowest size (`4 KiB`).

#### Memory allocation

In the buddy allocator, we can only make requests in terms of the available sizes; otherwise, it will give us the smallest chunk that is larger than or equal to the requested size.

Since our current order `0` has no chunks, the buddy allocator searches for a chunk in a higher-order freelist. The search goes up to order `11`, where we find the first non-empty freelist. A chunk is taken from this list's head, and it is split into 2 chunks of `2 MiB` each. These chunks are known as buddies. They can coalesce to re-form the original `4 MiB` chunk. They differ only in the lowest set bit of their starting addresses.

These 2 chunks are placed in the order `10` list, and the chunk with the lower address is picked and recursively broken down further in a similar fashion until we reach the target size, which in this case is `4 KiB`. After this allocation, our buddy allocator would look something like this:

<!-- photo -->

Each list would have one chunk.

#### Memory deallocation

Now, let's say our `4 KiB` frame was freed. It returns to the allocator. Since we have 2 buddies of the same order, they coalesce to form a chunk one order higher, and this continues recursively up to a hardcoded limit in the kernel.

If there were no buddy, the chunk would remain in the same order.

### HugeTLB

Now, I cannot directly touch memory from the buddy allocator from userspace. I am using `mmap`, and for `mmap` to be able to use huge pages, I first need to reserve the pages inside the kernel.

That is done using the following:

```bash
sudo sysctl -w vm.nr_hugepages=1024
```

This sets a mandatory requirement to have `1024` huge pages available for `mmapping`. However, the kernel makes a "best effort" attempt to fulfill this, which means the limit may not be fulfilled completely.

Internally, what happens is that the buddy allocator is requested to provide 1024 `2 MiB` chunks. These pages are stored in a freelist in `HugeTLB`.

When `mmapped` with explicit huge pages, the pages are reserved (basically promised), and the actual mapping and entry happen on the first page fault. Upon freeing, they are freed in reverse order.

In my experiment, I am doing a `memset` on the entire region immediately after the `mmapping`. So, all the page faults occur such that the mapped frames are in exactly the same order as they were in the freelist.

## PFN logging

To get closer to the answer, I decided to repeat the experiment, but this time, I logged the PFNs that were actually mapped to my virtual pages.

The results:

| Run / allocation position | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Allocation | shared p1 | shared p2 | shared p3 | shared p4 | shared p5 | shared p6 | shared p7 | shared p8 | shared p9 | shared p10 | shared p11 | shared p12 | shared p13 | shared p14 | shared p15 | shared p16 |
| Run 1 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 2 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 3 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 4 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 5 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 6 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 7 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 8 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 9 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 10 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 11 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 12 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 13 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 14 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 15 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 16 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 17 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 18 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |
| Run 19 | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Run 20 | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |


I confirmed that, for the same-mapping case, I was lucky enough to have no disturbance from an external process and to get exactly the same 16 huge pages in all runs.

Notice that we have contiguous frames at one end and not-so-contiguous frames at the other. Now, the same backing uses prefixes. When the contiguous frames were at the starting end, the spread in L3 was extremely nice, and I got fewer L3 misses (these were the conflict misses that are now gone). At the other end, there were some conflict misses.

## My theory

When I reboot, the buddy allocator has mostly non-fragmented memory, so there is more chance of getting pages that share page boundaries, i.e., that are contiguous.

Now, as you may know, the position of addresses in sets depends on physical address bits. Contiguous memory also spreads across the sets more nicely in general.

This gave me the L3 plateau. As time passes after reboot, the memory becomes too fragmented, and hence I get no nice spreading across the sets.

This was a nice experiment for digging deeper into modern processors and finding things out.
