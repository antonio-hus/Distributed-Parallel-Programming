# Warehouse Inventory System - Documentation

## Problem Statement

A wholesaler has several warehouses storing goods.
- We must keep track of the quantity of each product, in each of the warehouses.
- We have some moves between warehouses running concurrently, on several threads. Each move consists in moving some given amounts of some product types from a given source warehouse to another, given, destination warehouse.
- From time to time, as well as at the end, an inventory check operation shall be run. It shall check that, for each product type, the total amount of that product in all warehouses is the same as in the beginning.
-  Two moves involving distinct warehouses, or involving disjoint sets of products, must be able to be processed independently (without having to wait for the same mutex).

## Synchronization Strategies

### Strategy 1: Hybrid Hand-Over-Hand with Ordered Locking (Personal Idea)

**Approach:** Combines hand-over-hand locking efficiency with ordered lock acquisition for deadlock prevention.

**Lock Protocol:**
- **Case 1 (src < dst):** Lock source → deduct → lock destination → unlock source → add → unlock destination
- **Case 2 (src > dst):** Lock destination → lock source → validate & transfer → unlock both

**Deadlock Avoidance:** Always acquires locks in ascending warehouse index order

**Protected Invariants:**
- Product quantities remain non-negative
- Total inventory per product is conserved across all warehouses

**Advantages:**
- Minimal lock hold time in optimal case (src < dst)
- No deadlocks due to ordered locking
- Good balance between complexity and performance

**Disadvantages:**
- Two execution paths increase code complexity
- Reverse case requires holding both locks

### Strategy 2: Two-Point Locking with Ordered Acquisition

**Approach:** Acquire both warehouse locks at the start (in ascending index order), perform entire transaction, then release both.

**Lock Protocol:**
- Lock min(src, dst) → lock max(src, dst) → validate → deduct → add → unlock both

**Deadlock Avoidance:** Consistent total ordering of lock acquisition

**Protected Invariants:**
- Product quantities remain non-negative
- Total inventory per product is conserved
- Atomic transaction visibility (all-or-nothing)

**Advantages:**
- Simplest implementation (single execution path)
- Strong atomicity guarantees
- Easiest to reason about correctness

**Disadvantages:**
- Higher lock contention (both locks held throughout)
- Reduced concurrency compared to hand-over-hand

### Strategy 3: Hand-Over-Hand (Original - Deadlock Prone)

**Approach:** Lock source first, modify it, then acquire destination before releasing source.

**Lock Protocol:**
- Lock source → validate & deduct → lock destination → unlock source → add → unlock destination

**Deadlock Risk:**
```
Thread A: holds warehouse[1], wants warehouse[2]
Thread B: holds warehouse[2], wants warehouse[1]
Result: DEADLOCK (circular wait)
```

**Protected Invariants:**
- Product quantities (when successfully acquired)
- Inventory conservation (when deadlock doesn't occur)

**Advantages:**
- Minimal lock hold time
- Good theoretical performance

**Disadvantages:**
- **CRITICAL FLAW:** Prone to deadlocks with bidirectional transfers
- Not production-ready without additional mechanisms

## Mutex Protection Rules

### Mutex Responsibilities

**Warehouse::mtx protects:**
1. Read/write access to `Warehouse::products` map
2. Atomic read-modify-write sequences (check quantity → deduct/add)
3. Consistency of product quantities during transfers

### Locking Discipline

**Rule 1: Fine-Grained Locking**
- Each warehouse has its own mutex
- Transfers between warehouses W1 and W2 only lock those two warehouses
- Allows concurrent transfers: (W1→W2) and (W3→W4) proceed in parallel

**Rule 2: Ordered Lock Acquisition (Strategies 1 & 2)**
- Always lock warehouses in ascending index order: `warehouse[i]` before `warehouse[j]` where `i < j`
- Prevents circular dependencies and deadlocks

**Rule 3: All-or-Nothing Semantics**
- Validate ALL product quantities before modifying ANY warehouse state
- Abort entire transaction if any product has insufficient inventory

**Rule 4: Minimal Critical Sections**
- Hold locks only for the minimum necessary duration
- Release locks as soon as consistency allows

**Rule 5: Inventory Check Locking**
- Lock each warehouse individually and briefly
- Accumulate totals, release lock immediately
- Trades snapshot consistency for reduced contention

## Invariants

### Primary Invariant: Inventory Conservation
For each product `p`:
```
Σ(warehouse[i].products[p]) = initialTotals[p]
```
This must hold true after all operations complete.

### Secondary Invariants
1. **Non-negativity:** `warehouse[w].products[p] >= 0` for all w, p
2. **Atomicity:** Observers see either complete pre-transfer or post-transfer state
3. **Progress:** No deadlocks or livelocks (except Strategy 3)

## Performance Testing

### Test Configuration Parameters

| Parameter | Description | Typical Range |
|-----------|-------------|---------------|
| W | Number of warehouses | 4-64 |
| P | Number of products | 16-1024 |
| perWhPerProd | Initial quantity per product/warehouse | 100-5000 |
| numThreads | Worker threads | 2-16 |
| opsPerThread | Operations per thread | 1000-100000 |
| maxProductsPerMove | Products per transaction | 2-8 |
| maxDelta | Max quantity per product move | 2-10 |

### Hardware Configuration

**Test Platform:**
- **Hardware concurrency:** 20 threads
- **Pointer size:** 64-bit

### Performance Results

#### Small Scale Tests (Quick Validation)
**Config:** W=4, P=16, perWhPerProd=100, numThreads=2, opsPerThread=1000

| Strategy | Time (ms) | Ops/sec | Result  |
|----------|-----------|--------|---------|
| Hybrid Hand-Over-Hand | 3ms       | 666666.67 ops/sec       | PASS - Inventory invariant preserved        |
| Two-Point Locking | 2ms       | 1000000.00 ops/sec       | PASS - Inventory invariant preserved        |
| Hand-Over-Hand (Deadlock) | Deadlock  | Deadlock       | Deadlock |

#### Medium Scale Tests (Standard Stress)
**Config:** W=16, P=256, perWhPerProd=1000, numThreads=8, opsPerThread=20000

| Strategy | Time (ms) | Ops/sec  | Result                               |
|----------|-----------|----------|--------------------------------------|
| Hybrid Hand-Over-Hand | 229ms     | 698689.96 ops/sec         | PASS - Inventory invariant preserved |
| Two-Point Locking | 205ms     | 780487.80 ops/sec         | PASS - Inventory invariant preserved                                     |
| Hand-Over-Hand (Deadlock) | Deadlock  | Deadlock | Deadlock                             |

#### Large Scale Tests (High Contention)
**Config:** W=32, P=512, perWhPerProd=5000, numThreads=16, opsPerThread=50000

| Strategy | Time (ms) | Ops/sec           | Result                               |
|----------|-----------|-------------------|--------------------------------------|
| Hybrid Hand-Over-Hand | 875ms     | 914285.71 ops/sec | PASS - Inventory invariant preserved |
| Two-Point Locking | 878ms     | 911161.73 ops/sec                  | PASS - Inventory invariant preserved                                     |
| Hand-Over-Hand (Deadlock) | Deadlock  | Deadlock          | Deadlock                             |

#### Fine-Grained Tests (Low Contention)
**Config:** W=64, P=1024, perWhPerProd=500, numThreads=16, opsPerThread=100000

| Strategy | Time (ms) | Ops/sec            | Result                               |
|----------|-----------|--------------------|--------------------------------------|
| Hybrid Hand-Over-Hand | 857ms     | 1866977.83 ops/sec | PASS - Inventory invariant preserved |
| Two-Point Locking | 632ms     | 2531645.57 ops/sec                   | PASS - Inventory invariant preserved                                     |
| Hand-Over-Hand (Deadlock) | Deadlock  | Deadlock           | Deadlock                             |

## Conclusions

### Correctness
- **Production Ready:** Hybrid and Two-Point strategies
- **Not Safe:** Hand-Over-Hand (deadlock prone)
- All strategies maintain inventory invariants when they complete

### Performance Recommendations

1. **Use Hybrid** when there is high to very high contention
2. **Use Two-Point** when:
    - Simplicity and maintainability are priorities
    - Lower contention is expected
3. **Avoid Hand-Over-Hand** unless:
    - Adding deadlock detection/prevention mechanisms
    - Transfers are guaranteed unidirectional
    - High to very high contetion

### Key Takeaways

- Fine-grained locking (per-warehouse mutexes) enables good parallelism
- Ordered lock acquisition is essential for deadlock prevention
- Lock granularity vs. performance is a critical design tradeoff

---

**Author:** Antonio Hus 
**Date:** 02.10.2025
**Course:** Parallel and Distributed Programming - Lab 1