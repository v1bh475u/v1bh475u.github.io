+++
date = '2026-06-03T12:47:07+05:30'
draft = false
title = 'The L3 Plateau That Disappeared'
author = 'vibhatsu'
tags = ["cache-analysis", "systems-programming", "c++", "x86-64", "intel", "performance"]
cover = ""
coverCaption = ""
description = "I wanted to see the textbook cache-latency staircase on my own machine. The missing L3 plateau pulled me into TLBs, offsets, PMU counters, huge pages, and physical mappings."
readingTime = true
comments = true
keywords = ["cache", "cache analysis", "pointer chase", "LLC", "L3 cache", "huge pages", "PMU", "physical mapping"]
+++

Textbook cache-latency plots make memory look annoyingly well-behaved: grow the working set, and latency climbs through L1, L2, LLC, then DRAM. I tried to reproduce that staircase on my i5-12450HX with a dependent pointer chase and got something much uglier. The L3 plateau seemed to disappear way too early, and “probably just noise” was not a satisfying answer.

## My Beautiful Machine

My CPU was an `Intel Core i5-12450HX`. I ran the benchmark on logical CPU `7`, which is one SMT sibling on a P-core, and kept logical CPU `6` offline during the runs.
System summary:
| Category         |                                   Value |
| ---------------- | --------------------------------------: |
| CPU              |                   Intel Core i5-12450HX |
| Core topology    |       4 P-cores + 4 E-cores, 12 threads |
| Measurement CPU    |               P-core, logical CPU `7` |
| SMT sibling      |      logical CPU `6`, kept offline |
| L1D [P-core]| 48 KiB, 12-way, 64 B line |
| L2 [P-core]| 1.25 MiB private, 10-way |
| LLC| 12 MiB shared, 8-way |
| Kernel           | 7.0.11-zen1-1.1-zen |
| Compiler         |                   g++ 16.1.1|
| Frequency policy |    performance governor, turbo disabled |
| Pages tested     |     4 KiB, THP-requested, explicit 2 MiB huge pages |

The P-core cache numbers are the relevant ones here because the benchmark was pinned to a P-core. The E-core cache arrangement is different, but it is not part of this measurement path.

## Precautions and Setup
Before we can even begin, we need to do a **LOT** of isolation and cleaning if we want minimal noise in our measurements.
### The Compiler
Microbenchmarks are easy to accidentally optimize into nonsense, so the chase loop has to remain a real dependency chain and the final pointer has to stay live. Each load produces the address of the next load, and I also escape the final pointer through an empty inline-asm block so the compiler cannot treat the loop as dead code.

```cpp
  ChaseNode* current = head;
  for (std::uint64_t i = 0; i < iterations; ++i) {
    current = current->next;
  }
  asm volatile("" : "+r"(current) : : "memory");
```

### The OS
The OS can do a lot of shenanigans to destroy our measurements. 
It can pre-empt our program or migrate it to some other core, so I pinned my program to logical core `7`, offlined its SMT sibling logical CPU `6`, forced the `performance` governor, and disabled turbo. How I achieved this can be found in the repository scripts [here](https://github.com/v1bh475u/cache-walk/blob/master/scripts/apply_isolation.sh). I still repeated the experiment at least 30 times and used medians though.

### The hardware
Timing short regions on x86 is its own small circus. I bracketed the chase loop with `lfence; rdtsc` at the start and `rdtscp; lfence` at the end.
```cpp
inline std::uint64_t tsc_start() {
  _mm_lfence();
  const std::uint64_t t = __rdtsc();
  _mm_lfence();
  return t;
}

inline std::uint64_t tsc_stop() {
  unsigned int aux = 0;
  _mm_lfence();
  const std::uint64_t t = __rdtscp(&aux);
  _mm_lfence();
  return t;
}

//...

const std::uint64_t start = tsc_start();
chase_only_for_iterations(head, iterations);
const std::uint64_t stop = tsc_stop();
```
Even after this, there are extremely clever prefetchers that would render any logical access pattern into a more sequential one. To prevent that, I used random pointer chasing.
```cpp
  const std::size_t count = std::max<std::size_t>(1, bytes / sizeof(ChaseNode));
  nodes_.resize(count);

  std::vector<std::size_t> order(count);
  std::iota(order.begin(), order.end(), 0);

  if (order_kind == ChaseOrder::Random) {
    std::mt19937_64 rng(0xCACE'CA11ULL ^ static_cast<std::uint64_t>(bytes) ^ seed_salt);
    for (std::size_t i = order.size(); i > 1; --i) {
      std::uniform_int_distribution<std::size_t> dist(0, i - 1);
      std::swap(order[i - 1], order[dist(rng)]);
    }
  }

  for (std::size_t i = 0; i < order.size(); ++i) {
    const std::size_t current = order[i];
    const std::size_t next = order[(i + 1) % order.size()];
    nodes_[current].next = &nodes_[next];
  }
```
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

With the ritual purification done, here was the first sweep.

## The First Plot Did Not Cooperate

This baseline sweep used fresh allocations per working-set size, with normal `4 KiB` pages, 30 repetitions per point, and median cycles/access as the headline number.
![Baseline cache latency sweep](../images/cache-walk/cache_latency.png)

The L1 and L2 regions are pretty clear. The sequential chain stays low because it is a weak latency probe: it gives the hardware a predictable stream. The random dependent chain is the meaningful curve here. It serializes the load stream because each address depends on the previous load result.

That was exactly why I wanted it. The benchmark should expose the latency ladder more directly.

The problem was not just “no neat plateau.” The random dependent curve started climbing around `4 MiB` and was already near `200 cycles/access` by `8 MiB`, which is uncomfortably early for a `12 MiB` shared LLC.


At this point I did not know whether the benchmark was bad, the graph was misleading, the CPU was doing something subtle, or I had misunderstood the mental model. The first obvious suspect was translation overhead: random pointer chasing over a lot of small pages is exactly how you make a TLB miserable.

## Maybe It Was TLB Pressure

Random pointer chasing over a lot of 4 KiB pages is a good way to stress the TLB as well as the data cache. If translation overhead is doing most of the damage, larger pages should help because each TLB entry covers more memory and page walks become rarer. So I reran the sweep with three page modes: plain 4 KiB pages, THP-requested mappings, and explicit 2 MiB huge pages.

![Layout vs capacity](../images/cache-walk/layout_capacity.png)

So, huge pages did help but not in the way I expected. It does flatten out the curve but only on the top end! It reduces the latencies but does not change the shape of the curve and we are still stuck with the cliff.

So if the problem was not mostly translation overhead, the next suspect was placement.

## Maybe It Was Offset Weirdness

The next suspicious thing was address placement.

Maybe my benchmark was hitting some weird aliasing problem causing it to thrash in the LLC. So, I tried sweeping the virtual offset of the pointer chase. The idea was that if some offsets were unlucky, I could find a better one and see a plateau.

I allocated a chunk of size `working_set + max_offset` and shifted the chase start across the offset range.
![Offset sweep heatmap](../images/cache-walk/offset_heatmap.png)

I observed significant variation in latency for offset sweep from `3 MiB` to `8 MiB` but the differences were not large enough to explain the cliff. Also, we can see a huge drop in latency for `6-7 MiB`. However, the observation is not at all persistent. I used to find much of such strangely low latencies for one or two working sets.

| Size | Mode | Offset pair | Cycle/access Medians | Ratio |
| ---: | --- | ---: | ---: | ---: |
| 3 MiB | 4 KiB | 1024 vs 8192 | 76.11 vs 76.00 | 1.00x |
| 3 MiB | huge | 64 vs 2048 | 69.62 vs 69.74 | 1.00x |
| 8 MiB | 4 KiB | 128 vs 32768 | 229.09 vs 231.07 | 1.01x |
| 8 MiB | huge | 1024 vs 65536 | 216.14 vs 210.84 | 1.03x |
| 8 MiB | THP-requested | 1024 vs 16384 | 212.26 vs 207.37 | 1.02x |

As you see, the differences were small and definitely not enough to explain the massive jumping cliff.

## Latency Was Not Enough
So far, we have been relying on latency only to get our answers but I realized that we need to get our hands dirty and look at the PMU counters to get a better picture of what is going on.

Cycles per access can tell me that something got slower, but it does not say why. The benchmark would warm the chain, start counters, run the chase, stop counters, and normalize by the number of dependent loads. I didn't need to know everything but only a few key metrics:
- L2 miss event:  `l2_rqsts.demand_data_rd_miss`
- L3 hit event:   `mem_load_retired.l3_hit`
- L3 miss event:  `mem_load_retired.l3_miss`
- DTLB walks:     `dtlb_load_misses.walk_completed`

I made sure to reset these counters for each dependent loop run and for calculating the per-access values, I used the following formula:

```
dependent_loads = #nodes in the pointer chase * iterations
L2 miss/access = l2_rqsts.demand_data_rd_miss / dependent_loads
L3 hit/access = mem_load_retired.l3_hit / dependent_loads
L3 miss/access = mem_load_retired.l3_miss / dependent_loads
```
Once I had ROI counters, the mystery got sharper. One explicit-huge sweep looked like this:

| Size | Cycles/access | L2 miss/access | L3 hit/access | L3 miss/access |
| ---: | ---: | ---: | ---: | ---: |
| 3 MiB | 93.12 | 0.992517 | 0.805861 | 0.187899 |
| 4 MiB | 186.89 | 0.994819 | 0.366281 | 0.627930 |
| 5 MiB | 240.00 | 0.996566 | 0.144992 | 0.851131 |
| 7 MiB | 262.05 | 0.997888 | 0.017357 | 0.979984 |
| 8 MiB | 240.75 | 0.998416 | 0.105952 | 0.892009 |
| 12 MiB | 178.67 | 0.998891 | 0.353601 | 0.645162 |

So, the reason we are seeing a cliff is actually because there **is** a rapid increase in L3 misses and we are measuring the average latency of a mix of hits and misses. What mattered here was not some exact threshold, but the shape: the L3 miss fraction was jumping much earlier and much harder than I expected for a usable LLC regime. So, something is making the L3 miss fraction jump very early and very hard.

I noticed that on some of the runs, `12 MiB` had a lower (though still very high) L3 miss fraction than `8 MiB`. That was a hint that the working set size alone was not the only variable. Something else was at work. It could be due to other processes interfering as L3 is shared but I was suspicious because the same pattern repeated across multiple runs.

That suggested a new missing variable: not just **how much** memory I touched, but **which physical pages** I got.

## The Plot That Changed The Investigation

Then I ran the experiment that actually changed the story. In one version, each working-set size got a fresh allocation. In the other, I allocated one larger buffer and measured prefixes of that same backing: first `3 MiB`, then `4 MiB`, then `5 MiB`, and so on. Same pointer-chase logic, same page mode, same PMU setup; only the backing strategy changed.

The result:

![Fresh mappings vs same physical prefix](../images/cache-walk/l3_fresh_vs_same_prefix.png)

This was the first result that really broke the “size alone explains the cliff” model. The fresh-allocation runs picked up L3 misses much earlier, while prefixes of one fixed backing stayed well-behaved for longer. So working-set size was not the only variable anymore.

At that point I had one obvious suspect left: some kind of slice imbalance.

## A Slice Sanity Check
Textbook cache models assume a monolithic block of memory partitioned neatly into sets and ways. Real silicon does not work that way. A monolithic 12 MiB cache would be a massive bottleneck, so modern Intel CPUs physically divide the LLC into separate chunks called "slices," each managed by an uncore component called a C-Box.

When a core requests a physical address, the CPU routes it through an undocumented, proprietary hash function. This hash dictates exactly which physical slice owns that cache line, completely orthogonal to standard set indexing. The goal is to distribute traffic evenly across the silicon fabric.

This gave me my prime suspect: Slice Aliasing. What if my physical allocations were just astronomically unlucky? If the OS handed me physical pages that all happened to hash into the exact same C-Box, my benchmark would hammer a single slice to death while the rest of the 12 MiB cache sat empty.

To sanity-check that idea, I took `128` fresh `8 MiB` explicit-huge mappings with per-CBox lookup data and compared simple slice-balance metrics against L3 miss fraction. Translation overhead was already negligible here, so this was a clean LLC-level check.

Then I compared slice-balance metrics against L3 miss fraction:

| Slice metric | Spearman rho vs L3 miss fraction | Low-miss quartile median | High-miss quartile median |
| --- | ---: | ---: | ---: |
| max share | 0.214 | 0.325 | 0.350 |
| top2 share | -0.404 | 0.565 | 0.518 |
| max/mean | 0.214 | 1.949 | 2.102 |
| coefficient of variation | -0.281 | 0.526 | 0.512 |
| normalized entropy | 0.538 | 0.933 | 0.938 |

Using `abs(rho) >= 0.60` as a coarse “this actually looks strong” cutoff, nothing here qualified. High-miss mappings were not dramatically more concentrated than low-miss ones either. Slice imbalance might contribute noise, but it did not look like the main driver.

## The Great Revelation

Ok. This is super weird. Plain set pressure is not enough to explain the split, and slice imbalance did not rescue the story either. But the gap between fresh mappings and same-prefix runs is still right there. Why?

After a lot of rambling, I started to think backwards. What would make cache sets hold up massive memory? The addresses should have a nice spread across the sets rather than stacking up and self-evicting. 

I started wondering what physical layout would naturally result in a clean spread. The most obvious answer is just sequential, contiguous physical memory. If physical addresses just increment nicely, they sweep across the cache sets sequentially instead of stepping on each other's toes.

But how does this happen with our same backing prefix run? The thing is in same backing prefix run, we allocate a `16 MiB` chunk and then we measure the L3 misses for warm runs. And for fresh mapping, we allocate the required size and then free it for each size. Now, if by some means, the same backing was gaining almost always contiguous physical frames, it would have lower L3 misses and the increase in the misses towards the limit of the cache size would be explained by the live OS environment noise. Fresh mapping as per the data would be having non-contiguous physical pages.

But what would give this sort of advantage to same backing prefix mapping and not to fresh mapping? For that, I investigated how my OS allocates physical memory. My OS is `7.0.11-zen1-1.1-zen` which messes with CPU scheduling and I/O but it leaves the core memory management alone. It relies on mainline Linux buddy allocator.

This is where a new possible theory to explain arises.

What Linux does is use buddy allocator for physical memory in orders of frame sizes. The lowest size is `4 KiB` and largest is `4 MiB`. So, when we demand `12 KiB` memory, we are given an `8 KiB` chunk along with a `4 KiB` chunk, both being contiguous in physical memory as well. So, when we asked for `16 MiB`, it allocated physical memory from the highest order resulting in highly contiguous memory. While in case of fresh allocations, we demanded for tiny allocations making the memory more and more fragmented and thus giving us memory that is backed by non-contiguous physical memory.

Let's do some quick math. `12 MiB` with 8-ways gives us `1.5 MiB` each "way". In our `2 MiB` huge pages, the entire region is physically required to be contiguous. Thus, a `2 MiB` allocation backed by huge pages should give us a nice spread across cache sets and lower L3 misses. From our data, we observe that L3 misses only start becoming significant from `4 MiB`. I would consider this as a potential support to our theory.

So, now, we have our hypothesis: The early L3 cliff is probably hidden by OS fragmentation of physical memory causing conflict misses. So, if we were to allocate a ridiculous `32 MiB` chunk, we would get the memory from the highest order resulting in contiguous frames. And that might help us with improved spread. And hence, running the cache latency experiment on this memory region should give us at least some form of LLC regime other than cliff.

## The Moment of Truth
So I tested that prediction directly. I allocated one `32 MiB` explicit-huge buffer, measured prefixes of that same backing, and reran the original latency sweep with PMU counters. I also did these runs immediately after a reboot, when the system was more likely to hand out less fragmented physical memory. If backing layout was really the missing variable, this should recover a cleaner LLC regime.

![32 MiB explicit huge page run](../images/cache-walk/the-plateau.png)

Finally, after so much digging, we finally observe the plateau (partially)! Well, it does rise up a bit quickly but I would give that to the shared nature of L3. There are random processes interacting with the memory and so some of our accesses might be getting evicted by other processes and this becomes significant when we are pushing towards the limit of the cache.

## What Actually Changed
That `32 MiB` same-backing huge-page sweep did not magically give me a perfect textbook staircase. But it did something more useful: it brought back an actual LLC regime. The ugly `4–8 MiB` cliff from the fresh-allocation runs was not some fixed property of the CPU. It depended heavily on how the mapping got backed.

Cache microbenchmarking isn't just about working-set size and access. Page size, backing policy and physical placement can deform the picture badly enough to make LLC plateau look like it vanished. It did not vanish. I was just measuring it through a pretty terrible allocation strategy.

## References
- [Cache hierarchy of Intel Core i5-12450HX](https://www.intel.com/content/www/us/en/products/sku/228794/intel-core-i512450hx-processor-12m-cache-up-to-4-40-ghz/specifications.html)
- [Linux kernel physical memory documentation](https://www.kernel.org/doc/html/latest/mm/physical_memory.html)
- [Linux buddy allocator](https://students.mimuw.edu.pl/ZSO/Wyklady/06_memory2/BuddySlabAllocator.pdf)