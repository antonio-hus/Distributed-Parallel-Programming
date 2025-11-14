# Parallel Polynomial Multiplication — Lab 5 Documentation

## Problem Statement

### Goal
The goal of this lab is to implement a simple but non-trivial parallel algorithm.

### Requirement
Perform the multiplication of 2 polynomials. Use both the regular O(n²) algorithm and the Karatsuba algorithm, and each in both the sequential form and a parallelized form. Compare the 4 variants.

The documentation will describe:
- The algorithms
- The synchronization used in the parallelized variants
- The performance measurements

**Bonus:** Do the same for big numbers.

---

## Algorithms Overview

### Naive (O(n²)) Multiplication

#### Theory
- Given two polynomials:
  - A(x) = Σ(i=0 to n-1) aᵢxⁱ
  - B(x) = Σ(j=0 to m-1) bⱼxʲ
- The classical approach computes each result coefficient independently as:
  - Cₖ = Σ(i=0 to n-1) Σ(j=0 to m-1) [i + j = k] · aᵢbⱼ
- This is a double for-loop with quadratic time complexity.

#### Implementation
- **Sequential:** Simple nested loops.
- **Parallel:** Each thread computes contributions for a range of coefficients. Partial results are merged by summing up contributions.

#### Synchronization
- In the parallel version, each worker writes only to its local buffer, so no lock/mutex is required.
- The final result is produced by summing partial local results.

---

### Karatsuba Multiplication

#### Theory
- Recursively decomposes polynomials:
  - Split each polynomial into "low" and "high" halves:
    - A(x) = A_low(x) + A_high(x) · xᵐ
    - B(x) = B_low(x) + B_high(x) · xᵐ
  - The product:
    - A(x)B(x) = P₂x²ᵐ + (P₃ - P₂ - P₁)xᵐ + P₁
      - P₁ = A_low × B_low
      - P₂ = A_high × B_high
      - P₃ = (A_low + A_high) × (B_low + B_high)
- This reduces multiplication from four recursive calls (classical divide and conquer) to three, improving theoretical complexity to O(n^(log₂3)) ≈ O(n^1.58).

#### Implementation
- **Sequential:** Pure recursive divide-and-conquer, with a cutoff to switch to the naive method for small polynomials.
- **Parallel:** Uses std::async to perform subproblems P₁ and P₂ as futures, and merges when all finish. Depth limits are set to avoid thread oversubscription.

#### Synchronization
- Tasks are completely independent (no shared state in recursion).
- The only synchronization point is .get() on futures.

---

## Synchronization Strategy

**Naive Parallel:**
- Local buffers per thread — no shared writes.
- Final result merged at end — associative addition, no locks.

**Karatsuba Parallel:**
- All recursive calls act on independent data buffers.
- Only synchronization: future result retrieval — no mutexes, no data races.

---

## Benchmarking Approaches

**Benchmarking Function**
- Accepts a callable (std::function), allowing passing either a function pointer, lambda, or functor.
- Records execution wall-clock time via std::chrono.
- Prints first five coefficients of the result to verify correctness.

**Algorithm Call**
```
benchmark("Karatsuba Parallel",
[](const Poly& a, const Poly& b) { return multiply_karatsuba_par(a, b, 0); },
A, B);
```
- Uses a lambda to fix the third argument (`depth=0`) as required.

---

## Performance Measurements

| Implementation        | Degree | Time (ms) | First 5 Coefficients |
|---------------------- |--------|-----------|----------------------|
| Naive Sequential      | 4096   | ~87.06    | 2 7 16 30 50 ...     |
| Naive Parallel        | 4096   | ~24.68    | 2 7 16 30 50 ...     |
| Karatsuba Sequential  | 4096   | ~17.89    | 2 7 16 30 50 ...     |
| Karatsuba Parallel    | 4096   | ~6.34     | 2 7 16 30 50 ...     |

### Performance Analysis
- **Naive Sequential → Naive Parallel:** ~3.5× speedup from parallelization
- **Naive → Karatsuba (Sequential):** ~4.9× speedup from better algorithm
- **Naive Sequential → Karatsuba Parallel:** ~13.7× speedup from both optimizations
- **Karatsuba Sequential → Karatsuba Parallel:** ~2.8× speedup from parallelization

---

## Invariants and Correctness

- **Result Integrity:** All multiplication paths produce exactly the same output for identical input polynomials. Verified via first coefficients and optionally a checksum of the entire result.
- **No Data Races:** All parallelism is either read-only or merged in a thread-safe manner.
- **No Deadlocks:** Futures in Karatsuba join only independent threads.
- **Performance is Reproducible:** Use deterministic polynomial values for consistency.

---

## Advanced (Bonus) — Big Number Multiplication

- If polynomials use big integer types (e.g., `std::vector<__int128_t>` or GMP/Boost multi-precision), just substitute your `Coeff` typedef.
- All algorithms remain identical; only performance and memory costs increase.

---

## Conclusions

- **Parallel Karatsuba** offers the best scalability and performance for large degree polynomials.
- **Parallel naive** is easy to implement and benefits from multi-core, but loses to Karatsuba asymptotically.
- **Synchronization avoids all locks and races** by per-thread isolation and recursive independence.
- **Always verify both outputs and invariants** to ensure no parallelization bugs on boundary cases.

### Key Takeaways
1. Algorithm choice (O(n²) vs O(n^1.58)) matters more than parallelization for large inputs
2. Parallelization provides additional 2-3× speedup on top of algorithmic improvements
3. Lock-free designs with isolated state are simpler and more performant than shared-state approaches
4. Future-based parallelism (std::async) provides clean abstractions without manual thread management

---

**Author:** Antonio Hus  
**Date:** 21.10.2025  
**Course:** Parallel and Distributed Programming — Lab 5