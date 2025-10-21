# Parallel Matrix Multiplication - Documentation

## Problem Statement

Implement parallel matrix multiplication C = A x B using multiple thread distribution strategies.

- **Goal:** Divide matrix multiplication task between threads to observe caching effects and threading overhead
- **Core Operation:** Compute single elements of the resulting matrix independently
- **Key Characteristic:** Embarrassingly parallel problem - no coordination required between threads
- **Requirements:** 
  - Each thread computes multiple elements consecutively
  - No synchronization primitives (mutexes, atomics) needed
  - Measure and compare performance across different distribution strategies

## Matrix Multiplication Fundamentals

### Mathematical Definition
For matrices A(mxn) and B(nxp), the product C(mxp) is defined as:
```
C[i][j] = Σ(k=0 to n-1) A[i][k] x B[k][j]
```

### Computational Complexity
- **Sequential:** O(n³) operations for nxn matrices
- **Memory Access:** O(n²) elements read, O(n²) elements written
- **Cache Behavior:** Critical performance factor - determines practical speed

## Thread Distribution Strategies

### Strategy 1: Row-by-Row Distribution

**Approach:** Threads process consecutive elements in row-major order.

**Work Division Example (9x9 matrix, 4 threads):**
```
Thread 0: rows 0-1, elements 0-1 of row 2 (20 elements total)
Thread 1: remainder of row 2, row 3, elements 0-3 of row 4 (20 elements)
Thread 2: remainder of row 4, row 5, elements 0-5 of row 6 (20 elements)
Thread 3: remainder of row 6, rows 7-8 (21 elements)
```

**Memory Access Pattern:**
- **Matrix A:** Sequential row access (excellent spatial locality)
- **Matrix B:** Column access with stride (moderate cache misses)
- **Matrix C:** Sequential writes (excellent spatial locality)

**Cache Behavior:** EXCELLENT
- Consecutive memory access maximizes cache line utilization
- High cache hit rate for Matrix A (row-major storage)
- Predictable access patterns enable hardware prefetching

**Advantages:**
- Optimal cache performance among simple strategies
- Minimal false sharing (threads work on distant cache lines)
- Predictable memory bandwidth usage

**Disadvantages:**
- Column access to Matrix B causes some cache misses
- Not optimal for matrices larger than L3 cache

### Strategy 2: Column-by-Column Distribution

**Approach:** Threads process consecutive elements in column-major order.

**Work Division Example (9x9 matrix, 4 threads):**
```
Thread 0: columns 0-1, elements 0-1 of column 2 (20 elements)
Thread 1: remainder of column 2, column 3, elements 0-3 of column 4 (20 elements)
Thread 2: remainder of column 4, column 5, elements 0-5 of column 6 (20 elements)
Thread 3: remainder of column 6, columns 7-8 (21 elements)
```

**Memory Access Pattern:**
- **Matrix A:** Strided row access (many cache misses)
- **Matrix B:** Sequential column access (better locality)
- **Matrix C:** Strided writes (poor cache utilization)

**Cache Behavior:** MODERATE
- Column-major access on row-major storage causes some strided memory access
- Surprisingly competitive performance in real tests
- Hardware prefetcher partially compensates for stride patterns

**Advantages:**
- Better than expected performance on modern CPUs
- Demonstrates effectiveness of hardware prefetching

**Disadvantages:**
- Still slower than row-by-row on small matrices
- Non-optimal memory access pattern

### Strategy 3: Strided (K-th Element) Distribution

**Approach:** Each thread takes every K-th element (K = number of threads).

**Work Division Example (9x9 matrix, 4 threads):**
```
Thread 0: indices 0, 4, 8, 12, 16, 20, ... (every 4th element starting at 0)
Thread 1: indices 1, 5, 9, 13, 17, 21, ... (every 4th element starting at 1)
Thread 2: indices 2, 6, 10, 14, 18, 22, ... (every 4th element starting at 2)
Thread 3: indices 3, 7, 11, 15, 19, 23, ... (every 4th element starting at 3)
```

**Memory Access Pattern:**
- **Matrix A:** Irregular row access (maximum cache thrashing)
- **Matrix B:** Irregular column access (no spatial locality)
- **Matrix C:** Strided writes (false sharing risk)

**Cache Behavior:** VARIABLE
- Small matrices: surprisingly good performance due to entire dataset fitting in cache
- Large matrices: performance degrades due to poor locality

**Advantages:**
- Simple implementation
- Good load balancing

**Disadvantages:**
- Worst cache behavior in theory
- Performance unpredictable across different matrix sizes

### Strategy 4: Cache-Blocked with Optimized Thread Distribution

**Approach:** Combines cache tiling with row-major thread distribution for optimal performance.

**Core Concept:** Divide matrices into blocks that fit in CPU cache (typically 64x64 elements).

**Algorithm:**
```
For each row assigned to this thread:
For each column block of size BLOCK_SIZE:
For each K-dimension block:
Compute partial products for this block
All data fits in L1 cache during computation
```

**Block Size Selection:**
- **Typical:** 64x64 blocks for int32 matrices
- **Rationale:** 3 blocks (A, B, C) x 64² x 4 bytes ≈ 48KB ≈ L1 cache size
- **Trade-off:** Larger blocks = more reuse, but may exceed cache capacity

**Memory Access Pattern:**
- **Matrix A:** Block-wise row access (maximum temporal locality)
- **Matrix B:** Block-wise column access (fits in cache)
- **Matrix C:** Block-wise writes (accumulation in cache)

**Cache Behavior:** THEORETICAL OPTIMAL, PRACTICAL POOR
- Should provide best performance through cache reuse
- **Actual Results:** Significantly underperforms naive strategies
- **Root Cause:** Implementation overhead exceeds cache benefits for tested sizes

**Performance Anomaly:**
- Expected: Large speedup for large matrices
- Observed: Consistently slower than naive strategies
- Likely issues: Excessive function call overhead, suboptimal block processing

**Advantages (Theoretical):**
- Maximum cache reuse potential
- Scalable to very large matrices

**Disadvantages (Observed):**
- Implementation overhead dominates performance
- No performance benefit observed in testing
- Requires significant optimization to realize benefits

## Embarrassingly Parallel Nature

### Why No Synchronization Required

**Disjoint Memory Writes:**
- Each thread writes to completely separate elements of result matrix
- No overlap in output indices between threads
- No race conditions possible

**Read-Only Inputs:**
- Matrices A and B are never modified
- Multiple threads can safely read same memory locations
- No need for read synchronization

**Independence Property:**
- Each output element computed from input data only
- No dependencies between elements
- Computation order irrelevant to correctness

## Performance Testing

### Test Configuration

**Hardware Platform:**
- **CPU Cores:** 10 physical cores
- **Hardware Threads:** 20 threads (Hyper-Threading enabled)
- **L1 Cache:** 32 KB per core (typical)
- **L2 Cache:** 256 KB per core (typical)
- **L3 Cache:** ~20 MB shared (typical)
- **Memory:** DDR4
- **Architecture:** x86-64

**Test Parameters:**

| Parameter | Values Tested | Description |
|-----------|--------------|-------------|
| Matrix Size | 50x50, 100x100, 200x200, 500x500, 1000x1000 | Square matrices |
| Thread Count | 1, 4, 16, 32 | Worker threads |
| Element Type | int32 | 4 bytes per element |

### Performance Results

#### Small Matrix (50x50)

**Baseline (Single-threaded):** 0.878 ms

| Strategy | Threads | Time (ms) | Speedup | Throughput (elem/sec) |
|----------|---------|-----------|---------|----------------------|
| Row-by-Row | 1 | 1.039 | 0.85x | 2,406,160 |
| Row-by-Row | 4 | 0.483 | 1.82x | 5,175,983 |
| Row-by-Row | 16 | 0.920 | 0.95x | 2,717,391 |
| Row-by-Row | 32 | 1.418 | 0.62x | 1,763,047 |
| Column-by-Column | 1 | 0.837 | 1.05x | 2,986,858 |
| Column-by-Column | 4 | 0.597 | 1.47x | 4,187,605 |
| Column-by-Column | 16 | 0.934 | 0.94x | 2,676,660 |
| Column-by-Column | 32 | 1.610 | 0.55x | 1,552,795 |
| K-th Element | 1 | 0.792 | 1.11x | 3,156,566 |
| K-th Element | 4 | 0.423 | 2.08x | 5,910,165 |
| K-th Element | 16 | 0.915 | 0.96x | 2,732,240 |
| K-th Element | 32 | 1.347 | 0.65x | 1,855,976 |
| Cache-Blocked | 1 | 0.757 | 1.11x | 3,302,510 |
| Cache-Blocked | 4 | 0.817 | 2.08x | 3,059,976 |
| Cache-Blocked | 16 | 1.132 | 0.96x | 2,208,481 |
| Cache-Blocked | 32 | 1.376 | 0.65x | 1,816,860 |

**Observations:**
- Thread overhead dominates for small matrices
- Single-threaded baseline sometimes faster than multi-threaded (overhead > benefit)
- 4 threads shows best performance (2x speedup)
- 16+ threads cause severe slowdown due to context switching
- K-th element surprisingly fast with 4 threads (2.08x speedup)
- Cache-blocked shows no advantage (overhead dominates)

#### Medium Matrix (100x100)

**Baseline (Single-threaded):** 4.957 ms

| Strategy | Threads | Time (ms) | Speedup | Throughput (elem/sec) |
|----------|---------|-----------|---------|----------------------|
| Row-by-Row | 1 | 5.223 | 0.95x | 1,914,608 |
| Row-by-Row | 4 | 1.620 | 3.06x | 6,172,840 |
| Row-by-Row | 16 | 1.398 | 3.55x | 7,153,076 |
| Row-by-Row | 32 | 1.757 | 2.82x | 5,691,520 |
| Column-by-Column | 1 | 5.247 | 0.94x | 1,905,851 |
| Column-by-Column | 4 | 1.652 | 3.00x | 6,053,269 |
| Column-by-Column | 16 | 1.358 | 3.65x | 7,363,770 |
| Column-by-Column | 32 | 2.052 | 2.42x | 4,873,294 |
| K-th Element | 1 | 5.092 | 0.97x | 1,963,865 |
| K-th Element | 4 | 1.724 | 2.88x | 5,800,464 |
| K-th Element | 16 | 1.356 | 3.66x | 7,374,631 |
| K-th Element | 32 | 2.017 | 2.46x | 4,957,858 |
| Cache-Blocked | 1 | 5.407 | 0.97x | 1,849,454 |
| Cache-Blocked | 4 | 5.297 | 2.88x | 1,887,861 |
| Cache-Blocked | 16 | 5.362 | 3.66x | 1,864,976 |
| Cache-Blocked | 32 | 5.584 | 2.46x | 1,790,831 |

**Observations:**
- Parallelization begins to show clear benefits
- 16 threads achieves best performance (3.55-3.66x speedup)
- All simple strategies show similar performance
- Cache-blocked consistently slower (no implementation benefit observed)
- Column-by-column competitive with row-by-row

#### Medium-Large Matrix (200x200)

**Baseline (Single-threaded):** 39.566 ms

| Strategy | Threads | Time (ms) | Speedup | Throughput (elem/sec) |
|----------|---------|-----------|---------|----------------------|
| Row-by-Row | 1 | 40.101 | 0.99x | 997,481 |
| Row-by-Row | 4 | 11.204 | 3.53x | 3,570,154 |
| Row-by-Row | 16 | 6.693 | 5.91x | 5,976,393 |
| Row-by-Row | 32 | 10.350 | 3.82x | 3,864,734 |
| Column-by-Column | 1 | 42.871 | 0.92x | 933,032 |
| Column-by-Column | 4 | 10.309 | 3.84x | 3,880,105 |
| Column-by-Column | 16 | 5.458 | 7.25x | 7,328,692 |
| Column-by-Column | 32 | 9.914 | 3.99x | 4,034,698 |
| K-th Element | 1 | 44.300 | 0.89x | 902,935 |
| K-th Element | 4 | 10.444 | 3.79x | 3,829,950 |
| K-th Element | 16 | 5.828 | 6.79x | 6,863,418 |
| K-th Element | 32 | 10.462 | 3.78x | 3,823,361 |
| Cache-Blocked | 1 | 45.893 | 0.89x | 871,593 |
| Cache-Blocked | 4 | 41.475 | 3.79x | 964,436 |
| Cache-Blocked | 16 | 41.444 | 6.79x | 965,158 |
| Cache-Blocked | 32 | 41.725 | 3.78x | 958,658 |

**Observations:**
- Clear performance differentiation emerges
- 16 threads optimal (5.91-7.25x speedup)
- Column-by-column achieves best performance (7.25x at 16 threads)
- 32 threads shows performance degradation (over-subscription)
- Cache-blocked remains uncompetitive

#### Large Matrix (500x500)

**Baseline (Single-threaded):** 629.851 ms

| Strategy | Threads | Time (ms) | Speedup | Throughput (elem/sec) |
|----------|---------|-----------|---------|----------------------|
| Row-by-Row | 1 | 638.660 | 0.99x | 391,445 |
| Row-by-Row | 4 | 157.508 | 4.00x | 1,587,221 |
| Row-by-Row | 16 | 75.156 | 8.38x | 3,326,414 |
| Row-by-Row | 32 | 72.495 | 8.69x | 3,448,514 |
| Column-by-Column | 1 | 653.899 | 0.96x | 382,322 |
| Column-by-Column | 4 | 155.227 | 4.06x | 1,610,545 |
| Column-by-Column | 16 | 72.111 | 8.73x | 3,466,877 |
| Column-by-Column | 32 | 73.393 | 8.58x | 3,406,319 |
| K-th Element | 1 | 650.748 | 0.97x | 384,173 |
| K-th Element | 4 | 153.505 | 4.10x | 1,628,611 |
| K-th Element | 16 | 71.465 | 8.81x | 3,498,216 |
| K-th Element | 32 | 71.050 | 8.86x | 3,518,649 |
| Cache-Blocked | 1 | 672.101 | 0.97x | 371,968 |
| Cache-Blocked | 4 | 656.466 | 4.10x | 380,827 |
| Cache-Blocked | 16 | 681.110 | 8.81x | 367,048 |
| Cache-Blocked | 32 | 663.947 | 8.86x | 376,536 |

**Observations:**
- Near-linear scaling up to 16 threads
- All simple strategies perform similarly (8.38-8.86x speedup)
- 32 threads shows continued improvement (memory bandwidth not saturated)
- K-th element achieves best performance (8.86x at 32 threads)
- Cache-blocked still shows no benefit

#### Very Large Matrix (1000x1000)

**Baseline (Single-threaded):** 5247.621 ms

| Strategy | Threads | Time (ms) | Speedup | Throughput (elem/sec) |
|----------|---------|-----------|---------|----------------------|
| Row-by-Row | 1 | 5271.450 | 1.00x | 189,701 |
| Row-by-Row | 4 | 1245.797 | 4.21x | 802,699 |
| Row-by-Row | 16 | 587.433 | 8.93x | 1,702,322 |
| Row-by-Row | 32 | 517.628 | 10.14x | 1,931,889 |
| Column-by-Column | 1 | 5079.287 | 1.03x | 196,878 |
| Column-by-Column | 4 | 1230.209 | 4.27x | 812,870 |
| Column-by-Column | 16 | 536.567 | 9.78x | 1,863,700 |
| Column-by-Column | 32 | 533.676 | 9.83x | 1,873,796 |
| K-th Element | 1 | 5145.427 | 1.02x | 194,347 |
| K-th Element | 4 | 1360.115 | 3.86x | 735,232 |
| K-th Element | 16 | 580.358 | 9.04x | 1,723,074 |
| K-th Element | 32 | 567.250 | 9.25x | 1,762,891 |
| Cache-Blocked | 1 | 5369.652 | 1.02x | 186,232 |
| Cache-Blocked | 4 | 5313.308 | 3.86x | 188,207 |
| Cache-Blocked | 16 | 5134.526 | 9.04x | 194,760 |
| Cache-Blocked | 32 | 5118.898 | 9.25x | 195,355 |

**Observations:**
- Maximum speedup achieved: 10.14x (row-by-row, 32 threads)
- Column-by-column competitive (9.83x at 32 threads)
- K-th element shows degradation at higher thread counts
- Cache-blocked consistently poor (implementation issue confirmed)
- Hyper-threading shows benefit (32 threads > 16 threads)

## Conclusions
### Strategy Comparison

**Winner: Row-by-Row (for large matrices)**
- Best performance at 1000x1000: 10.14x speedup
- Consistent performance across all sizes
- Simple implementation

**Runner-up: Column-by-Column**
- Often outperforms row-by-row at medium sizes
- Best performance at 200x200: 7.25x speedup
- Demonstrates effectiveness of hardware prefetching

**Competitive: K-th Element**
- Strong performance on small-medium matrices
- Good load balancing
- Degrades on very large matrices

**Poor Performer: Cache-Blocked**
- Consistently slowest across all configurations
- No benefit observed from blocking
- Implementation overhead exceeds cache benefits

### Performance Surprises

**Unexpected Findings:**
1. Column-by-column often fastest (contradicts cache theory)
2. K-th element competitive despite poor locality
3. Cache-blocked catastrophically slow (implementation issue)
4. Hardware prefetching more effective than expected

---

**Author:** Antonio Hus  
**Date:** 14.10.2025  
**Course:** Parallel and Distributed Programming - Lab 3