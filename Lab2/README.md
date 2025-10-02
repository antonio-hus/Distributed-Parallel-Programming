# Scalar Product Producer-Consumer - Documentation

## Problem Statement

Create two threads, a producer and a consumer, with the producer feeding the consumer.

Requirement: Compute the scalar product of two vectors.

Create two threads. The first thread (producer) will compute the products of pairs of elements - one from each vector - and will feed the second thread. The second thread (consumer) will sum up the products computed by the first one. The two threads will behind synchronized with a condition variable and a mutex. The consumer will be cleared to use each product as soon as it is computed by the producer thread.

## Solution Architecture

### Synchronization Mechanism

**SharedData Structure:**
- `mtx`: Mutex protecting all shared state
- `cv`: Condition variable for signaling between threads
- `product_ready`: Flag indicating a product is available for consumption
- `done`: Flag indicating producer has finished computing
- `current_product`: The computed product value awaiting consumption

### Thread Communication Protocol

#### Producer Protocol
1. Compute product locally (outside critical section)
2. Acquire lock and wait until previous product consumed (`product_ready == false`)
3. Store product in `current_product` and set `product_ready = true`
4. Release lock and notify consumer
5. After all products: set `done = true` and notify

#### Consumer Protocol
1. Acquire lock and wait until `product_ready == true` or `done == true`
2. If product available: copy to local variable, set `product_ready = false`
3. Release lock and notify producer
4. Add product to running sum (outside critical section)
5. Repeat until all products consumed

### Protected Invariants

**Mutex protects:**
1. Read/write access to `product_ready`, `done`, and `current_product`
2. Atomicity of check-and-modify sequences
3. Consistency between product value and ready flag

**Condition Variable ensures:**
1. Consumer never reads stale/uninitialized products
2. Producer never overwrites unconsumed products
3. No missed signals (atomic wait + lock release)

---

**Author:** Antonio Hus  
**Date:** 02.10.2025  
**Course:** Parallel and Distributed Programming - Lab 2