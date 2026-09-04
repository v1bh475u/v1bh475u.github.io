+++
date = '2026-09-04T12:07:07+05:30'
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

In the previous blog [here](https://vibhatsu.me/posts/cache-walk), I tried to replicate the textbook cache latency plot on my machine. I had to tweak a lot of knobs before finally finding the plot, but the cause of the plot was completely unexplained. In this post, I am going to continue my journey of analyzing why the plot did not appear in the first experiment itself!

## A Quick Recap

Last time, for the plot, I changed three conditions:

- Turned off the prefetchers;
- Did the experiment directly after boot; and
- Allocated a single chunk and used prefixes from it.

So, for starters, I focused on simpler and more tangible tests _(whispers: wrong move)_. I repeated the same experiment but with fresh allocations.

![Fresh allocation plot](../images/cache-digging/fresh_run_curves.svg)

Well, I was not expecting there to be much difference, except that since there was a fresh allocation per size in each run, there had to be much more noise. As you may notice, there are multiple graphs: one graph per run, with 20 runs in total. I then decided to see how each size performed across the runs. They seemed to align and trace out only 2 graphs. So, I decided to see if this was actually the case.

![Fresh degradation](../images/cache-digging/fresh_run_degradation.svg)

And it was! There seemed to be an alternating pattern of good and bad runs. I decided to do the same for the same-backing one and got this:

![Same backing degradation](../images/cache-digging/same_run_degradation.svg)

It too had the same alternating pattern but with a smaller magnitude! I re-ran it multiple times, and the same sort of alternating pattern occurred. This did not match my understanding at all!

## A not-so-tiny tour on how Linux manages memory

First of all, I am going to abstract away a **LOT** to simplify the explanation, so please take this with a grain of salt. Hopefully, this will help explain why I was so surprised.

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

If you are new to allocators, I recommend visiting [my blog](https://vibhatsu.me/posts/dynamic-allocator), as I am going to use some terminology relevant to that. Please keep in mind that this is a simplified model, and I am not going into migration types as they are not very relevant to my case.

The smallest block that can be allocated is 4 KiB. A buddy allocator has multiple freelists, each containing blocks whose sizes are powers of 2, as shown below:

![Buddy allocator before the example allocation](../images/cache-digging/buddy-init.svg)

Each list contains blocks of a particular size, given by the following formula:

\[
    size = 2^{order} \times 4\text{ KiB}
\]

The core idea behind the buddy allocator is that it divides memory into 2 buddies that can coalesce to form a block one order higher. Let's take an example.

Let's say we have `8 MiB` of memory and the highest order of our buddy allocator is `10`, i.e., the highest-order freelist contains blocks of size \(2^{10} \times 4\text{ KiB} = 4\text{ MiB}\), as in the above image. So, we have 11 freelists in total, from `0` to `10`.

Hence, the `8 MiB` memory is initially divided into multiple blocks of `4 MiB` each. There are 8 MiB / 4 MiB = 2 blocks in the highest order. (Note: I am not considering the DMA32 zone reservation or any other reservation in this explanation.)

With this initial setup, let's say our allocator gets a request for the lowest size (`4 KiB`).

#### Memory allocation

In the buddy allocator, we can only request a block of a particular order and not any random size.

Since our current order `0` has no blocks, the buddy allocator searches for a block in a higher-order freelist. The search goes up to order `10`, where we find the first non-empty freelist. A block is taken from the head of this list and split into 2 blocks of `2 MiB` each. These blocks are known as buddies. They can coalesce to re-form the original `4 MiB` block. They are literally adjacent in physical memory.

One of these blocks is placed in the order `9` list, and the one with the lower address is recursively broken down in a similar fashion until we reach the target size, which in this case is `4 KiB`. After this allocation, our buddy allocator looks something like this:

![Buddy allocator after splitting an order-10 block for an order-0 allocation](../images/cache-digging/buddy-after-alloc.svg)

Each list has one block.

#### Memory deallocation

Now, let's say our `4 KiB` frame is freed. It returns to the allocator. Since we have 2 buddies of the same order, they coalesce to form a block one order higher, and this continues recursively up to a hard-coded limit in the kernel.

If its buddy is not free, the block remains in the same order.

### HugeTLB

Now, I cannot directly touch memory from the buddy allocator from userspace. I am using `mmap`, and for an `mmap` allocation to use huge pages, I first need to reserve the pages inside the kernel.

That is done using the following:

```bash
sudo sysctl -w vm.nr_hugepages=1024
```

This forces the kernel to make a persistent pool of `1024` huge pages available for `mmapping` (the total remains persistent, i.e., total = free + in-use). However, it makes a "best-effort" attempt to fulfill this, which means the target may not be reached.

Internally, what happens is that the buddy allocator is requested to provide 1024 `2 MiB` blocks. These pages are stored in a freelist in `HugeTLB`.

When `mmapped` with explicit huge pages, the pages are reserved (basically promised), and the actual mapping and entry in the TLB happen upon the first page fault. Upon freeing, they are freed in reverse order. This is not a necessary thing. It is just a coincidence of the current implementation.

In my experiment, I do a `memset` on the entire region immediately after the `mmapping`. So, all the page faults occur such that the mapped frames are in exactly the same order as they were in the freelist.

## My surprise

I assumed that I was getting mostly contiguous mappings, so there should not be much difference between odd and even runs, as the pages are still the same but in reverse order as having contiguous memory usually implies that the addresses are spread evenly across the sets, right?

Nope! Here's why: Let's say I get the following order on my odd run:

```
V0 - P0, V1 - P1, ..., V15 - P15
```
Here, `V0`, `V1`, ... are my virtual pages for the run and `P0`, `P1`, ... are the actual physical frames that are mapped to the virtual pages.

Now, on my even run, since the order is reversed, I would get:

```
V0 - P15, V1 - P14, ..., V15 - P0
```

Again, this is considering no external process messed with HugeTLB between the runs. So, for prefixes, the mapping changes.

Here, `2 MiB` uses `V0`; `4 MiB` uses `V0`, `V1`; `6 MiB` uses `V0`, `V1`, `V2`; and so on. Now, since the physical frames supporting these virtual addresses differ, they can show different behaviours. So, if one end is mostly contiguous memory but the other end is not so contiguous, I would observe some worse behaviour. But if I assume mostly contiguous memory across all the frames across all the frames across all the frames across all the frames across all the frames across all the frames across all the frames across all the frames across all the frames, the results still should not be bad.

## PFN logging

To get closer to the answer, I decided to repeat the experiment, but this time, I logged the PFNs that were actually mapped to my virtual pages.

To do this, I first extracted virtual page number, read the corresponding entry from `/prof/self/pagemap` and decode the physical frame number by discarding the lower 21 bits.

The results:

| Run / allocation position | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Odd runs (1, 3, ..., 19) | 2746 | 2907 | 2783 | 2902 | 3063 | 3066 | 3057 | 3056 | 3067 | 3068 | 3069 | 3070 | 3071 | 3072 | 3073 | 3074 |
| Even runs (2, 4, ..., 20) | 3074 | 3073 | 3072 | 3071 | 3070 | 3069 | 3068 | 3067 | 3056 | 3057 | 3066 | 3063 | 2902 | 2783 | 2907 | 2746 |


For the same-mapping case, I was lucky enough to have no disturbance from an external process in `HugeTLB` and to get exactly the same 16 huge pages in all runs.

Notice that we have contiguous frames at one end and not-so-contiguous frames at the other (as I feared). Now, the same backing uses prefixes. When the contiguous frames were at the starting end, the spread in L3 was extremely nice, and I got fewer L3 misses (these were the conflict misses that are now gone). At the other end, there were some conflict misses.

I noticed a similar pattern for fresh allocation runs as well. The only differences were that there were 61 pages per run and that the `10 MiB` allocation was in the middle, getting the exact same pages irrespective of the order.

Now, I have a reason why the graph showed up only after reboot and not in my first experiment.

## My theory

When I reboot, the buddy allocator has mostly non-fragmented memory, so there is a greater chance of getting pages that share page boundaries, i.e., that are contiguous.

Now, as you may know, the position of addresses in sets depends on physical address bits. Contiguous memory also spreads across the sets more nicely in general.

Now, Intel did introduce slices that basically act orthogonally to sets and use hashing to fill the places, but I am optimistic that they won't ruin the performance gain from spreading across sets.

This gave me the L3 plateau. As time passes after reboot, the memory becomes too fragmented, and hence I get no nice spreading across the sets.

This was a nice experiment for digging deeper into modern processors and finding things out.

---

## References

- [Linux kernel documentation: Physical Memory](https://docs.kernel.org/mm/physical_memory.html)

- [Linux kernel documentation: HugeTLB Pages](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html)

- [Linux Buddy Allocator](https://grimoire.carcano.ch/blog/memory-management-the-buddy-allocator/)
