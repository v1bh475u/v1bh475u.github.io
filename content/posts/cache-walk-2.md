+++
date = '2026-07-14T12:07:07+05:30'
draft = true
title = 'A curious case of alternating good and bad allocations'
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

Last time, for the plot, I changed three conditions:

- Turned off the prefetchers;
- Did the experiment directly after boot; and
- Allocated a single chunk and used prefixes from it.

So, for starters, I focused on simpler and more tangible tests _(whispers: wrong move)_. I repeated the same experiment but with fresh allocations.

![Fresh allocation plot](../images/cache-digging/fresh_run_curves.svg)

Well, I was not expecting there to be much difference, except that since there is a fresh allocation per size in a run, there has to be much more noise. As you may notice, there are multiple graphs: one graph per run, with 20 runs in total. I then decided to see how each size performed across the runs. And they seem align and trace out 2 graphs only. So, I decided to see if this was actually the case.

![Fresh degradation](../images/cache-digging/fresh_run_degradation.svg)

And it was! There seems to be an alternating pattern of good runs and bad runs. I decided to do the same for the same-backing one and got this:

![Same backing degradation](../images/cache-digging/same_run_degradation.svg)

It too had the same alternating pattern but with a smaller magnitude! I re-ran it multiple times, and the same sort of alternating pattern occurred. This did not match my understanding at all!

## A not-so-tiny tour on how linux manages memory

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

If you are new to allocators, I recommend visiting [my blog](https://vibhatsu.me/posts/dynamic-allocator) as I am going to use a few terminologies relevant to that. Please keep in mind, this is a simplified model and I am not going into migration types as it is not much relevant to my case.

The smallest block that can be allocated is 4 KiB. A buddy allocator has multiple freelists, each containing blocks whose sizes are powers of 2, as shown below:

![ff](../images/cache-digging/buddy-init.svg)

Each list contains only a particular size of blocks given by the following formula:

\[
    size = 2^{order} \times 4\text{ KiB}
\]

The core idea behind the buddy allocator is that it divides memory into 2 buddies that can coalesce togetherto form one order higher block. Let's take an example.

Let's say we have our `8 MiB` memory and the highest order of our buddy allocator is `10`, i.e., the highest order freelist contains blocks of size \(2^{10} \times 4\text{ KiB} = 4\text{ MiB}\), as in the above image. So, we have in total 11 freelists from `0` to `10`.

Hence, the `8 MiB` memory is initially divided into multiple blocks of `4 MiB` each. There will be 8 MiB / 4 MiB = 2 blocks in the highest order. (Note: I am not considering the DMA32 zone reservation or any other reservation for this explanation.)

With this initial setup, let's say our allocator gets a request for the lowest size (`4 KiB`).

#### Memory allocation

In the buddy allocator, we can only make requests in terms of what order block we want; otherwise, it will give us the smallest block that is larger than or equal to the requested size.

Since our current order `0` has no blocks, the buddy allocator searches for a block in a higher-order freelist. The search goes up to order `10`, where we find the first non-empty freelist. A block is taken from this list's head, and it is split into 2 blocks of `2 MiB` each. These blocks are known as buddies. They can coalesce to re-form the original `4 MiB` block. They are literally adjacent in physical memory.

One of these blocks is placed in the order `9` list, and the one with the lower address is further recursively broken down in a similar fashion until we reach the target size, which in this case is `4 KiB`. After this allocation, our buddy allocator would look something like this:

![ff](../images/cache-digging/buddy-after-alloc.svg)

Each list would have one block.

#### Memory deallocation

Now, let's say our `4 KiB` frame was freed. It returns to the allocator. Since we have 2 buddies of the same order, they coalesce to form a block one order higher, and this continues recursively up to a hardcoded limit in the kernel.

If there were no buddy, the block would remain in the same order.

### HugeTLB

Now, I cannot directly touch memory from the buddy allocator from userspace. I am using `mmap`, and for `mmap` to be able to use huge pages, I first need to reserve the pages inside the kernel.

That is done using the following:

```bash
sudo sysctl -w vm.nr_hugepages=1024
```

This forces the kernel to make a persistent pool of `1024` huge pages available for `mmapping` (the total remains persistent, i.e., total = free + in-use should be persistent). However, it makes a "best effort" attempt to fulfill this, which means the limit may not be fulfilled completely.

Internally, what happens is that the buddy allocator is requested to provide 1024 `2 MiB` blocks. These pages are stored in a freelist in `HugeTLB`.

When `mmapped` with explicit huge pages, the pages are reserved (basically promised), and the actual mapping and entry in the TLB happen on the first page fault. Upon freeing, they are freed in reverse order. This is not a necessary thing. It is just a coincidence of the current implementation.

In my experiment, I am doing a `memset` on the entire region immediately after the `mmapping`. So, all the page faults occur such that the mapped frames are in exactly the same order as they were in the freelist.

## My surprise

I assumed that I was getting mostly contiguous mappings so, there should not be much difference between odd and even runs as the pages are still the same but in reverse order, right?

Nope! Here's why: Let's say I get the following order on my odd run:
```
V0 - P0, V1 - P1, ..., V15 - P15
```
Now, on my even run, since the order is reversed, I would get:
```
V0 - P15, V1 - P14, ..., V15 - P0
```
Again, this is considering no external process messed with HugeTLB in between the runs. So, for prefixes, the mapping is changing. 

Here, `2 MiB` gets `V0`, `4 MiB` gets `V0`, `V1`, `6 MiB` gets `V0`, `V1`, `V2` and so on. Now, since the phyiscal frame supporting these virtual addresses differs, they can show different behaviours.But if I assume mostly contiguous memory, the results still should not be bad.

## PFN logging

To get closer to the answer, I decided to repeat the experiment, but this time, I logged the PFNs that were actually mapped to my virtual pages.

The results:

| Run / allocation position | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Odd runs (1, 3, ..., 19) | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Even runs (2, 4, ..., 20) | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |


For the same-mapping case, I was lucky enough to have no disturbance from an external process in `HugeTLB` and to get exactly the same 16 huge pages in all runs.

Notice that we have contiguous frames at one end and not-so-contiguous frames at the other. Now, the same backing uses prefixes. When the contiguous frames were at the starting end, the spread in L3 was extremely nice, and I got fewer L3 misses (these were the conflict misses that are now gone). At the other end, there were some conflict misses.

I noticed a similar pattern for fresh allocation runs as well. Only difference was that per run, the number of pages were 61 and that size `10 MiB` was in the middle, getting the exact same pages irrespective of the order.

Now, I have a reasoning why the graph showed up only after reboot and not in my first experiment.

## My theory

When I reboot, the buddy allocator has mostly non-fragmented memory, so there is more chance of getting pages that share page boundaries, i.e., that are contiguous.

Now, as you may know, the position of addresses in sets depends on physical address bits. Contiguous memory also spreads across the sets more nicely in general.

Now, Intel did introduced slices that basically acts orthogonally to sets and uses hashing to fill the places, but I am optimistic that they won't ruin the performance gain by sets spreading.

This gave me the L3 plateau. As time passes after reboot, the memory becomes too fragmented, and hence I get no nice spreading across the sets.

This was a nice experiment for digging deeper into modern processors and finding things out.

---

## References

- [Linux kernel documentation: Physical Memory](https://docs.kernel.org/mm/physical_memory.html)

- [Linux kernel documentation: HugeTLB Pages](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html)

- [Linux Buddy Allocator](https://grimoire.carcano.ch/blog/memory-management-the-buddy-allocator/)

