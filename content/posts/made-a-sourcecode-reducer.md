+++
date = '2026-03-06T00:25:01+05:30'
title = 'Building a C Source-code Reducer (with a CPU Cycle Twist)'
author = 'vibhatsu'
tags = ["C","Compilers","Project-showcase"]
cover = ""
coverCaption = ""
description = "A look into building a tool that minimizes C code while trying to keep the CPU cycle count invariant"
readingTime = true
comments = true
keywords = ["reducer", "gcov", "clang", "systems-programming"]
+++

One of my professors recently gave us a slightly evil assignment: build a C source-code reducer that keeps the CPU cycle behavior intact. So the goal wasn't just to shrink the program. It also had to take roughly the same number of CPU cycles to run.

The task was assigned in groups of two. You can check out the full source code and architecture [on GitHub](https://github.com/v1bh475u/c-reducer).

---

# The Task
The goal was to create a source-code reducer for single-file `C` programs that don't require `stdin`. Here were the "rules of the game":
- **Behavioral Integrity:** Must not change the program's output (`stdout`/`stderr`).
- **Performance Invariance:** Try to maintain the same CPU cycle count as the original.
- **No Cheating:** Simply deleting everything and `printf`-ing the original output and padding up the CPU cycles is invalid.
- **Tooling:** Existing tools could be used for subtasks, but the orchestration had to be ours.

### Evaluation Criteria
1. **LoC Reduction:** The tool that deletes the most lines wins. The number of lines is counted by number of `;`. So removing whitespace from the entire program won't work.
2. **Cycle Penalty:** Significant deviations from the original CPU cycles result in a penalty.

# Initial Exploration
While exploring reducers, our professor suggested looking at a few existing tools and techniques used in program reduction. That led us to read about several approaches, including the classic **Delta Debugging** algorithm.

Delta debugging repeatedly removes chunks of input and checks if the program still behaves correctly. If removing a chunk doesn't break the test, that chunk wasn't necessary.

It works surprisingly well, but it treats the program mostly as **text**. It doesn't understand the structure of the code.

We wanted something that operated at least partially on **program structure**!

# Our Approach
We settled on a pass-based architecture, much like a compiler.
Each pass analyzes the current source and attempts a transformation. After every transformation, the program is recompiled and validated to ensure the behavioral constraint still holds.
The overall flow is as below:

![Reduction Pipeline](../images/made-a-src-reducer/pipeline.svg)

## The Reduction Passes

* **Coverage-based:** We used `gcov` to identify lines that are never executed. If a line never runs, it’s usually safe to trash.
* **LSP & Diagnostics:** We used `clangd` to find unused variables, unreachable code, and redundant functions.
* **Adjacent Statement Merging:** We looked for opportunities to condense code without changing logic.
    ```c
    int a; int b; -> int a, b;
    a += 4; b += a; -> a += 4, b += a;
    ```
* **Identifier Counting:** We tracked usage of `typedefs`, `struct` members, and `enums`. If an identifier is never referenced, the definition is pruned.

## The Iterative Pipeline
One pass often "unlocks" another. For example, deleting a function might make a specific `struct` definition unused.
Our first idea was to keep running the passes until nothing changed anymore.

Unfortunately, this quickly became extremely slow.

## The Parsing Bottleneck

We then realized the issue was re-parsing. 
Because each pass modified the code, we had to re-parse for the next pass almost every time. On `csmith` generated files over `500` lines, it took forever! To make the tool usable, we replaced the fixed-point iteration with a **fixed number of iterations**. In each iteration, we rerun the entire pass pipeline in hopes of further reducing the program.
You could still configure the iterations via CLI args depending upon how much time you have.

## The "Cycle" Problem
Reducing code usually makes it run faster, which was actually a problem here because of the CPU cycle constraint. We looked at two ways to handle this:
- **Conservative Approach**: This involved testing the cycles and ensuring that it does not fall below a certain threshold after application of each pass.
- **Aggressive Approach**: This involved aggressively reducing the code and after the iterations, we inject useless computations to recover the cycles.

We generated `csmith` testcases with different seeds and configurations for testing. We measured the cycles using `perf stat` and to reduce the noise in the measurement, we took average over **5 runs**.

After testing on `csmith` testcases, we found out that the **conservative approach** failed to reduce the code significantly. In most cases, it just applied coverage-based reductions! Also, the cycle testing increased the time involved in reducing.
With the **aggressive + padding approach**, we achieved much better reductions. In my opinion, the **conservative approach** was fine as well but since the focus of task was more on reduction instead of preserving the cycles strictly, the **aggressive approach** was the better choice.
To recover the lost cycles, we inject a simple busy-loop into the program:
```c
{
    volatile int _pad = 0;
    for (long _i = 0; _i < N; _i++)
        _pad += (int)_i;
}
```
Here, `N` is calculated using analysis of before and after CPU cycle measurements.

# Example Reduction Produced by the Tool
### Original Program

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int MyInt;
typedef char MyChar;

void dead_helper() {
    int z = 99;
    printf("never called %d\n", z);
}

int compute(int n) {
    return n + 1;
}

int main() {
    int x;
    int y;
    int unused_var = 100;
    x = compute(5);
    y = compute(10);
    printf("%d %d\n", x, y);
    return 0;
}
```

### Reduced Program

```c
#include <stdio.h>

int compute(int n) {
    return n + 1;
}

int main() {
    int x, y;
    x = compute(5), y = compute(10), printf("%d %d\n", x, y);
    return 0;
}
```

# Evaluation Results

We tested the reducer on programs generated by **`csmith`**.

The repository includes a script that runs the reducer across multiple programs and validates correctness and performance.

```
=== Summary ===
Total:   100
Passed:  93
Failed:  0
Skipped: 7
```

The skipped tests correspond to programs that **exceeded the reducer's configured timeout limit**. Some `csmith` programs can take a very long time to reduce due to complex control flow and heavy pointer usage, so we had to include a timeout in the tool to keep the evaluation manageable.

### Performance Metrics

| Metric                  | Value              |
| ----------------------- | ------------------ |
| Average code reduction  | 32%                |
| Average CPU cycle ratio | 101.3% of original |
| Tests requiring padding | 26                 |
| Average reduction time  | 55.98s             |

Some observations:

* The reducer removes roughly **one-third of the code on average**.
* The padding strategy keeps execution time very close to the original.
* Around **26% of programs required artificial padding**.
* Each reduction still takes **about a minute**, largely due to repeated parsing and compilation.

Even after multiple passes, `csmith` programs often resist aggressive reduction because of heavy pointer arithmetic and complex expressions.

# Future Improvements
- Since our bottleneck time is spent in parsing, we could use something like **incremental parsing** to avoid re-creating ASTs after almost every transform.
- More aggressive AST-level simplifications could enable deeper reductions.
- If possible, we can explore multiple candidates in parallel.

# Wrap up
Even though this tool is far from production-grade, building it was a great exercise in combining static analysis, coverage data, and performance measurement into a reduction pipeline.

**Check out the project here:** [github.com/v1bh475u/c-reducer](https://github.com/v1bh475u/c-reducer)

If you have ideas on how to handle the parsing latency or a more elegant way to "pad" cycles without bloating the file, I’d be curious to hear them.
