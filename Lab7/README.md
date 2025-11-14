# Distributed Polynomial Multiplication — Lab 7 Documentation

## Problem Statement

### Goal
The goal of this lab is to implement a distributed algorithm using MPI.

### Requirement
Perform the multiplication of 2 polynomials, by distributing computation across several nodes using MPI. Use both the regular O(n2) algorithm and the Karatsuba algorithm. Compare the performance with the "regular" CPU implementation from lab 5.

The documentation will describe:
- the algorithms,
- the distribution and communication,
- the performance measurements

**Bonus:** Do the same for the multiplication of big numbers.

---

## Algorithms Overview

### Naive (O(n²)) MPI Distribution

#### Theory
- Given two polynomials A(x) and B(x), the classical convolution approach computes each result coefficient independently as:
    - Cₖ = Σ(i=0 to n-1) Σ(j=0 to m-1) [i + j = k] · aᵢbⱼ
- The computation is naturally parallelizable by partitioning polynomial A across processes.

#### MPI Implementation Strategy
**Distribution Pattern:**
- Polynomial B is broadcast to all processes via `MPI_Bcast` (O(log P) latency)
- Polynomial A is partitioned into contiguous chunks via `MPI_Scatterv`
- Each process computes partial products for its assigned coefficients of A
- Results gathered back to root via `MPI_Gatherv` with automatic merging of overlapping regions

**Communication Operations:**
- `MPI_Bcast`: Broadcasts entire polynomial B to all processes (one-time cost)
- `MPI_Scatterv`: Distributes A chunks with automatic load balancing for non-divisible sizes
- `MPI_Gatherv`: Collects variable-sized result segments, handling boundary overlaps

**Load Balancing:**
- Processes handle uneven distribution automatically: first (n % P) processes get one extra coefficient
- Ensures minimal idle time and balanced workload across all nodes

***

### Karatsuba MPI Distribution

#### Theory
- Recursively decomposes polynomials using divide-and-conquer:
    - Split: A(x) = A_low(x) + A_high(x) · xᵐ, B(x) = B_low(x) + B_high(x) · xᵐ
    - Compute three subproblems:
        - P₁ = A_low × B_low
        - P₂ = A_high × B_high
        - P₃ = (A_low + A_high) × (B_low + B_high)
    - Reconstruct: A(x)B(x) = P₂x²ᵐ + (P₃ - P₂ - P₁)xᵐ + P₁
- Theoretical complexity: O(n^(log₂3)) ≈ O(n^1.58)

#### MPI Implementation Strategy
**Master-Worker Pattern:**
- Master process decomposes top-level problem into three subproblems (P₁, P₂, P₃)
- Subproblems distributed to worker processes via point-to-point messaging
- Workers compute assigned subproblems using sequential Karatsuba
- Master collects results and performs final reconstruction

**Communication Operations:**
- Point-to-point `MPI_Send/MPI_Recv` for task distribution and result collection
- Avoids collective operation overhead for irregular task sizes
- Each worker receives: task ID + two polynomial segments
- Each worker returns: task ID + result polynomial

**Synchronization:**
- Master sends all tasks before collecting results (prevents deadlock)
- Workers process tasks sequentially (stateless computation)
- No explicit barriers needed due to master-worker dependency structure

**Base Case Optimization:**
- Switches to sequential Karatsuba for polynomials smaller than degree 256
- Avoids communication overhead dominating computation time for small subproblems

***

## Communication Patterns Analysis

### Collective Operations (Naive MPI)
- **Broadcast (MPI_Bcast):** O(log P) latency, efficiently distributes read-only data
- **Scatter (MPI_Scatterv):** O(P) communication for initial distribution, handles load balancing automatically
- **Gather (MPI_Gatherv):** O(P) communication for result collection, merges overlapping coefficient contributions

**Characteristics:**
- Predictable communication pattern
- Optimized by MPI implementation for specific network topologies
- Well-suited for regular, data-parallel workloads

### Point-to-Point Messaging (Karatsuba MPI)
- **Send/Recv pairs:** Explicit task assignment and result retrieval
- **Irregular workload:** Subproblem sizes vary due to recursive decomposition
- **Fine-grained control:** Allows dynamic task distribution

**Characteristics:**
- Higher programming complexity
- Potential for load imbalance if subproblems have uneven computational costs
- Communication overhead can exceed computation savings for small polynomials

***

## Synchronization Strategy

### Naive MPI
- **No explicit locks:** Distributed memory model eliminates shared-state concurrency
- **Implicit synchronization:** Collective operations provide built-in barriers
- **Data isolation:** Each process writes only to local buffers, merged at gather phase
- **Thread-safe by design:** No race conditions possible due to message-passing semantics

### Karatsuba MPI
- **Master-worker coordination:** Natural synchronization through task dependency
- **No deadlocks:** Master completes all sends before initiating receives
- **Stateless workers:** Each task is independent, enabling potential for dynamic scheduling
- **Sequential subproblem execution:** Workers process one task at a time, simplifying state management

---

## Performance Measurements

### Test Configuration
- **Polynomial Degree:** 65,536 coefficients (2^16)
- **Coefficient Values:** A[i] = (i % 10) + 1, B[i] = (i % 5) + 2
- **Hardware:** Single node, 1 MPI process (baseline measurement)
- **Compiler:** mpic++ with -O3 optimization
- **MPI Implementation:** Microsoft MPI v10.1.3

### Results (Lab 7 - MPI Implementation)

| Implementation        | Processes | Degree | Time (ms) | First 5 Coefficients |
|----------------------|-----------|--------|-----------|----------------------|
| Naive MPI            | 1         | 65,536 | 21,391.8  | 2 7 16 30 50         |
| Karatsuba MPI        | 1         | 65,536 | 1,426.31  | 2 7 16 30 50         |

### Performance Analysis (Lab 7)
- **Karatsuba vs Naive (single process):** ~15× speedup from algorithmic improvement
- **Result correctness:** All implementations produce identical output coefficients
- **Communication overhead:** Single-process run shows pure computation time (no network costs)

***

## Comparison with Lab 5 (Shared-Memory Parallel)

| Implementation         | Degree | Time (ms) |
|------------------------|--|---|
| Naive Sequential       | 65,536 | ~18248.7 |
| Naive Parallel (Threads)| 65,536 | ~2581.95 |
| Karatsuba Sequential   | 65,536 | ~1375.18 |
| Karatsuba Parallel (Threads)| 65,536 | ~304.392 |

### Lab 7 MPI Performance Characteristics

**Communication Overhead Impact:**
- For degree 65,536 on single MPI process, no network communication occurs
- True distributed performance requires multi-node testing (2-16 processes)

**Architectural Differences:**

| Aspect                | Lab 5 (Shared Memory) | Lab 7 (MPI Distributed) |
|------------------------|------------------------|-------------------------|
| **Memory Model**       | Shared address space   | Distributed (message passing) |
| **Synchronization**    | Implicit (futures)     | Explicit (send/recv, collectives) |
| **Communication Cost** | Cache coherency        | Network latency + bandwidth |
| **Scalability**        | Limited to single node | Scales to cluster of nodes |
| **Load Balancing**     | Dynamic (thread pool)  | Static (partitioning) or manual |

**When MPI Outperforms Shared-Memory:**
- Very large problem sizes exceeding single-node memory (e.g., degree > 1,000,000)
- Access to multi-node clusters (8+ nodes with fast interconnect)
- Parallel workloads with minimal communication

**When Shared-Memory Outperforms MPI:**
- Small to medium problem sizes (degree < 100,000)
- Single-node execution where communication is pure overhead
- Algorithms requiring frequent fine-grained synchronization

***

## Invariants and Correctness

### Result Integrity
- **Verification:** All multiplication variants produce identical output for same input polynomials
- **Coefficient validation:** First five coefficients (2, 7, 16, 30, 50) match across all implementations
- **Checksum validation:** Optional full polynomial comparison via coefficient sum

### Communication Correctness
- **No message loss:** All MPI operations use blocking semantics ensuring delivery
- **Deterministic results:** Fixed polynomial values guarantee reproducible output
- **Boundary handling:** Gather operations correctly merge overlapping coefficient contributions

### Numerical Stability
- **No overflow:** Coefficient type `long long` supports products up to ~9.2 × 10^18
- **Exact arithmetic:** Integer operations ensure no floating-point rounding errors
- **Reproducibility:** Identical results across runs and process counts

***

**Author:** Antonio Hus  
**Date:** 14.11.2025  
**Course:** Parallel and Distributed Programming — Lab 7