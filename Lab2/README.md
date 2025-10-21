# Scalar Product Producer-Consumer - Documentation

## Problem Statement

Create two threads, a producer and a consumer, with the producer feeding the consumer.

Requirement: Compute the scalar product of two vectors.

Create two threads. The first thread (producer) will compute the products of pairs of elements - one from each vector - and will feed the second thread. The second thread (consumer) will sum up the products computed by the first one. The two threads will be synchronized with a condition variable and a mutex. The consumer will be cleared to use each product as soon as it is computed by the producer thread.

## Solution Architecture

### Synchronization Mechanism

**SharedData Structure:**
- `mtx`: Mutex protecting all shared state
- `cv_producer`: Condition variable for signaling when deque has space
- `cv_consumer`: Condition variable for signaling when deque has data
- `product_deque`: Bounded deque storing computed products
- `max_deque_size`: Configurable buffer size
- `done`: Flag indicating producer has finished computing

### Thread Communication Protocol

#### Producer Protocol
1. Compute product locally (outside critical section)
2. Acquire lock and wait until deque has space (`size < max_deque_size`)
3. Push product to deque and release lock
4. Notify consumer via condition variable
5. After all products: set `done = true` and notify

#### Consumer Protocol
1. Acquire lock and wait until deque has data or `done == true`
2. If product available: pop from deque
3. Release lock and notify producer (space available)
4. Add product to running sum (outside critical section)
5. Repeat until deque empty and producer done

### Protected Invariants

**Mutex protects:**
1. Read/write access to `product_deque` and `done` flag
2. Deque size checks and modifications
3. Consistency between deque state and flags

**Condition Variables ensure:**
1. Producer blocks when deque is full (backpressure)
2. Consumer blocks when deque is empty (no busy-waiting)
3. No missed signals (atomic wait + lock release)

## Performance Analysis

### Experimental Results

**Configuration:** Vector size = 10,000 elements, 3 runs averaged per deque size

| Deque Size | Average Time (ms) | Notes |
|------------|-------------------|-------|
| 1          | 135ms             |       |
| 2          | 64ms              |       |
| 5          | 23.33ms           |       |
| 10         | 14.33ms           |       |
| 50         | 4.33ms            |       |
| 100        | 3.66ms            |       |
| 500        | 3ms               |       |
| 1000       | 3ms               |       |

---

**Author:** Antonio Hus  
**Date:** 07.10.2025  
**Course:** Parallel and Distributed Programming - Lab 2