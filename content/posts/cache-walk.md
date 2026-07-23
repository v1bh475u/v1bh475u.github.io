+++
date = '2026-07-14T12:07:07+05:30'
draft = false
title = 'The L3 Plateau That Disappeared'
author = 'vibhatsu'
tags = ["cache-analysis", "systems-programming", "c++", "x86-64", "intel", "performance"]
cover = ""
coverCaption = ""
description = "I wanted to see the textbook cache-latency staircase on my own machine. The missing L3 plateau pulled me into TLBs, offsets, PMU counters, huge pages, and physical mappings."
readingTime = true
comments = true
keywords = ["cache", "cache analysis", "pointer chase", "LLC", "L3 cache", "TLB", "huge pages", "HugeTLB", "PMU", "RDTSC", "microbenchmark", "physical mapping"]
+++

Textbook cache-latency plots have this extremely simplistic model of memory: as the working-set size increases, the latency increases forming a staircase graph with L1, L2, L3 and DRAM as steps. Something like this:

![ideal graph](../images/cache-walk/ideal_cache_latency.svg)

I always wanted to observe this graph on my own machine and just got enough time to play around with it. The results were not what I expected. This blog shows my journey in finding that graph.

## My Beautiful Machine

My CPU was an `Intel Core i5-12450HX`. The other details are as follows:

| Category         |                                   Value |
| ---------------- | --------------------------------------: |
| CPU              |                   Intel Core i5-12450HX |
| Core topology    |       4 P-cores + 4 E-cores, 12 threads |
| Measurement CPU  |               P-core, logical CPU `7` |
| SMT sibling      |      logical CPU `6`, kept offline |
| L1D [P-core]     | 48 KiB, 12-way, 64 B line |
| L2 [P-core]       | 1.25 MiB private, 10-way |
| LLC              | 12 MiB shared, 8-way |
| Kernel           | 7.0.11-zen1-1.1-zen |
| Compiler         |                   g++ 16.1.1 |
| Frequency policy |    performance governor, turbo disabled |
| Pages tested     |     4 KiB, THP-requested, explicit `2 MiB` huge pages |

A bit about my chip: Intel chips have evolved to have two different types of cores: P-cores and E-cores. P-cores are performance cores, each equipped with its own private L1 and L2 caches and optimized for performance. These are mostly responsible for high energy consumption. E-cores, on the other hand, are efficiency-driven. They share L2 cache. For my purpose, I was interested in conducting the experiment on a P-core as it follows the same topology used in textbooks: private L1 and L2 caches, and a shared L3 cache.

Also, Intel has enabled Hyper-Threading on P-cores, which allows concurrent hardware threads on the same physical core. How this is achieved is that even though there are 4 P-cores, the OS sees 8. Each P-core behaves like 2 logical CPUs. Of course, since the private resources of the physical core are shared between the 2 logical siblings, I kept the SMT sibling of my pinned core offline in order to reduce the noise while observing the L1 and L2 latencies.

## Precautions and Setup

Before I started measuring anything, I had to do a **LOT** of isolation and cleaning to keep the noise down.

### The Compiler

Modern compilers are smart enough to detect and optimize memory accesses. So, I cannot use sequential memory access, or else it might get vectorized or become something else entirely. To prevent this, I used a random pointer chase. I allocate the working set, and then, starting from a random address, the next access address is determined by the value at the current address. I also used an inline asm block so that the compiler won't mark the loop as dead code. I padded the node size to one cache line so that one node access touches exactly one cache line.

```cpp
  
  struct alignas(64) ChaseNode {
    ChaseNode* next = nullptr;
    std::byte padding[64 - sizeof(ChaseNode*)]{};
  };

  ChaseNode* current = head;
  for (std::uint64_t i = 0; i < iterations; ++i) {
    current = current->next;
  }
  asm volatile("" : "+r"(current) : : "memory");
```

### The OS
The OS can do a lot of shenanigans to destroy the measurements.

It can pre-empt my program or migrate it to some other core, so I pinned my program to logical core `7`, offlined its SMT sibling logical CPU `6`, forced the `performance` governor, and disabled turbo. The CPU governor decides the CPU frequency. In other modes, the frequency can change mid-execution. The `performance` governor keeps the frequency at its maximum non-turbo frequency. What is turbo frequency? Another Intel quirk. It allows the frequency to rise temporarily above the base level when there is thermal and power headroom. So, obviously, I needed to disable turbo boost as well.

How I achieved this can be found in the repository scripts [here](https://github.com/v1bh475u/cache-walk/blob/master/scripts/apply_isolation.sh). I still repeated the experiment at least 30 times and used median as a hedge.

### The hardware
Timing short regions on x86 is its own small circus. There is out-of-order execution, which means the loads might happen before or after the time measuring, giving us incorrect readings. To prevent this, I bracketed the chase loop with `lfence; rdtsc` at the start and `rdtscp; lfence` at the end.
```cpp
  auto start = fenced_rdtsc();
  chase(head, iterations);
  auto stop = fenced_rdtscp();
```
`rdtsc` reads the CPU's timestamp counter, and `rdtscp` does the same but only after all prior instructions have completed. `lfence` is an execution barrier that makes sure that the measured loop does not move across the timing boundary.

Even after this, there are extremely clever prefetchers that could render any logical access pattern into a more sequential one. To prevent that, I used purely random order generation for the pointer chase.

```cpp
  std::iota(order.begin(), order.end(), 0);
  std::shuffle(order.begin(), order.end(), rng);

  for (std::size_t i = 0; i < order.size(); ++i) {
    nodes[order[i]].next = &nodes[order[(i + 1) % order.size()]];
  }
```

After ensuring all the above precautions, I was left with the following details:
| Knob                  |                                Value |
| --------------------- | -----------------------------------: |
| Node size             | 64 B, one pointer-sized node padded to one cache line |
| Ring coverage         | One node per cache line across the requested working set |
| Sample                | One independently rewired and timed pointer-chase run at a fixed configuration |
| Random seed           | Deterministic per sample: `0xCACECA11 ^ bytes ^ seed_salt` |
| Baseline warmup       | `100000` dependent loads before timing |
| Baseline measurement  | `5000000` dependent loads per sample |
| ROI PMU warmup        | Usually `20` full ring traversals before enabling counters |
| ROI PMU measurement   | Usually `10` full ring traversals with counters enabled |
| Repetitions per point | `30` for the main plotted sweeps unless stated otherwise |
| Aggregation           | Median cycles/access is the headline number |
| Error bars            | p10-p90 interval where shown |

Now, let's see some nice graphs!

## Where is my L3 regime?

This baseline sweep used fresh allocations per working-set size, with normal `4 KiB` pages, 30 repetitions per point, and median cycles/access as the headline number.
![Baseline cache latency sweep](../images/cache-walk/cache_latency.svg)

The L1 and L2 regions are pretty clear. The sequential chain does not show the staircase rise because it has an extremely predictable access pattern: each next access is prefetched into L1d directly, thus giving us the L1 latency for all the working sets.

The issue here is the L3 cache. I ran the experiment multiple times and still got the same result! According to my specs, the L2 region ends at `1.25 MiB` and L3 begins. It ends at `12 MiB`. Instead, I got a cliff! The graph jumps to around `275 cycles/access` starting from `4 MiB` itself!

This made me wonder what was wrong. I know that L3 is shared and there will be noise due to a live OS, but this was just too much.

The first obvious suspect was translation overhead: random pointer chasing over a lot of small pages is exactly how you make a TLB miserable.

## Maybe It Was TLB Pressure

Random pointer chasing over a lot of `4 KiB` pages is a good way to stress the TLB as well as the data cache. If translation overhead is doing most of the damage, larger pages should help because each TLB entry covers more memory and page walks become rarer. So I reran the sweep with three page modes: plain `4 KiB` pages, THP-requested mappings, and explicit `2 MiB` huge pages. THP-requested mappings are not necessarily backed by actual huge pages, but when available, the kernel tries to use them.

![Layout vs capacity](../images/cache-walk/layout_capacity.svg)

From the graph, it is clear that explicit huge pages and THP-requested mappings have slightly lower latencies but still contain the same sharp rise as the normal page graph.

Now, if the issue is not translation overhead, then maybe it is just that I am getting really unlucky with addresses.

## Maybe It Was Offset Weirdness

Maybe my benchmark was getting some unfortunate placement, causing many of the addresses to be in the same sets/slices(another Intel quirk but not very relevant yet), and after averaging those unlucky ones, I might be getting the cliff.

So, I allocated a chunk of size `working_set + max_offset` and shifted the chase start across the offset range.

![Offset sweep heatmap](../images/cache-walk/offset_heatmap.svg)

I saw some noise in the `3–8 MiB` offset sweep, but the actual ratios were too tiny to even consider: 1.00x to 1.03x across every size and mode I tested.

| Size | Mode | Offset pair | Cycle/access Medians | Ratio |
| ---: | --- | ---: | ---: | ---: |
| 3 MiB | 4 KiB | 1024 vs 8192 | 76.11 vs 76.00 | 1.00x |
| 3 MiB | huge | 64 vs 2048 | 69.62 vs 69.74 | 1.00x |
| 8 MiB | 4 KiB | 128 vs 32768 | 229.09 vs 231.07 | 1.01x |
| 8 MiB | huge | 1024 vs 65536 | 216.14 vs 210.84 | 1.03x |
| 8 MiB | THP-requested | 1024 vs 16384 | 212.26 vs 207.37 | 1.02x |

As you can see, the differences were small and definitely not enough to explain the massive jumping cliff.

## Latency Was Not Enough

So far, I have been relying on latency only to get my answers, but I realized that I need to get my hands dirty and look at the PMU counters to get a better picture of what is going on. These are Intel's helpers that can show what exactly is going on in the hardware.

The benchmark would warm the chain, start counters, run the chase, stop counters, and normalize by the number of dependent loads. I used the following metrics:
- L2 miss event:  `l2_rqsts.demand_data_rd_miss`
- L3 hit event:   `mem_load_retired.l3_hit`
- L3 miss event:  `mem_load_retired.l3_miss`
- DTLB walks:     `dtlb_load_misses.walk_completed`

I reset these counters before every run. To compute per-access values, I used the following formulae:

```
dependent_loads = #nodes in the pointer chase * loop_iterations
L2 miss/access = l2_rqsts.demand_data_rd_miss / dependent_loads
L3 hit/access = mem_load_retired.l3_hit / dependent_loads
L3 miss/access = mem_load_retired.l3_miss / dependent_loads
```

These are the results I got for running the experiment with the counters:

| Size | Cycles/access | L2 miss/access | L3 hit/access | L3 miss/access |
| ---: | ---: | ---: | ---: | ---: |
| 3 MiB | 93.12 | 0.992517 | 0.805861 | 0.187899 |
| 4 MiB | 186.89 | 0.994819 | 0.366281 | 0.627930 |
| 5 MiB | 240.00 | 0.996566 | 0.144992 | 0.851131 |
| 7 MiB | 262.05 | 0.997888 | 0.017357 | 0.979984 |
| 8 MiB | 240.75 | 0.998416 | 0.105952 | 0.892009 |
| 12 MiB | 178.67 | 0.998891 | 0.353601 | 0.645162 |

Interesting... The cliff is because I am getting a lot of L3 misses where I shouldn't. At `4 MiB` itself, the miss rate is more than 50%! Also, strangely enough, the miss rate drops a little from `8 MiB`. I believe the miss rate should ideally stay close to zero up to `8 MiB` and then rise,considering the OS noise.

So, if I figure out what was causing so many misses, maybe the plateau will appear. The gradual dip in latencies from `8 MiB` indicates that the hidden mechanism is not a function of working set size but something else.

## False Hope

In the original experiment, I was using a fresh allocation per working-set size. So, I was not just increasing the working-set size; I was also potentially changing the physical backing per size. These fresh allocations could be introducing bad mappings for some sizes, making them look bad. So, to test whether these fresh allocations truly affected my previous results or not, I decided to conduct a test.

In one version, each working-set size got a fresh allocation. In the other, I allocated one larger buffer and measured prefixes of that same backing: first `3 MiB`, then `4 MiB`, then `5 MiB`, and so on. Same pointer-chase logic, same page mode, same PMU setup; only the backing strategy changed.

The result:

![Fresh mappings vs same physical prefix](../images/cache-walk/l3_fresh_vs_same_prefix.svg)

The result seemed promising! Maybe the allocation mattered here. The fresh-allocation runs showed a rapid rise in L3 misses, while the prefixes of one fixed backing stayed low, as the misses ideally should.

Just for my own sanity, I rebooted the machine once again and re-ran the experiment...

![No difference in fresh vs same physical prefix](../images/cache-walk/fresh_vs_same_fail.svg)

And now, there seems to be absolutely no difference at all between the two! I re-ran the experiment a couple of times, and almost every time, I got the same results! The gap between the two was never large enough to be considered significant. The graphs both showed higher L3 misses as I increased the number of runs, but no difference! Great way to waste my 2 weeks!

So, it is not the fresh vs same mapping prefix. However, I noticed another trend. Whenever I ran the experiment directly after boot, both showed significantly lower L3 misses up to `10 MiB` at the very least.

So, just to check, I decided to do the following:

I rebooted again, disabled the prefetchers, allocated one explicit `32 MiB` huge-page-backed region, and measured prefixes of that same mapping.

## Reboot and Redemption

![The plateau](../images/cache-walk/the-plateau.svg)

There it is — the plateau! From roughly `2 MiB` to `8 MiB` the curve stays low and flat before it starts climbing (as expected due to OS noise).

I finally achieved my goal to clearly plot the staircase graph. So, I should be happy, right? Wrong!

Now, I have no idea why this worked! But I have some speculation. From the previous sections, I know that backing strategy did not give me the answer but since I haven't tested this for fresh allocations per size under the same setup, it is worth a try.

The remaining suspects are post-boot memory state and prefetchers.

What might be happening is immediately after a reboot, the allocator might hand the benchmark relatively uncontended physical memory because most of it is still unused, producing a cleaner spread across cache sets.

Either this, or it was prefetchers messing up my readings by picking up accidental patterns and prefetching the wrong lines. It seems unlikely, but it is possible.

Now, this seems like a rabbit hole to dive into. But for now, I am going to end here as the goal of this post was to get the plateau. I will dig into the reasoning in the next post!

## References

- [Cache hierarchy of Intel Core i5-12450HX](https://www.intel.com/content/www/us/en/products/sku/228794/intel-core-i512450hx-processor-12m-cache-up-to-4-40-ghz/specifications.html)

- [Intel Performance Monitoring events](https://github.com/intel/perfmon)

- [Transparent HugePages](https://www.kernel.org/doc/html/latest/admin-guide/mm/transhuge.html)