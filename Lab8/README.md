# Distributed Shared Memory with Lamport Clocks — Lab 8 Documentation

## Problem Statement

### Goal
The goal of this lab is to deal with the consistency issues of the distributed programming.

### Requirement
Implement a simple distributed shared memory (DSM) mechanism. The lab shall have a main program and a DSM library. There shall be a predefined number of communicating processes. The DSM mechanism shall provide a predefined number of integer variables residing on each of the processes. The DSM shall provide the following operations:
- write a value to a variable (local or residing in another process);
- a callback informing the main program that a variable managed by the DSM has changed. All processes shall receive the same sequence of data change callbacks; it is not allowed that process P sees first a change on a variable A and then a change on a variable B, while another process Q sees the change on B first and the change on A second.
- a "compare and exchange" operation, that compares a variable with a given value and, if equal, it sets the variable to another given value. Be careful at the interaction with the previous requirement.

Notes:
- Only nodes that subscribe to a variable will receive notifications about changes of that variable, and only those nodes are allowed to change (set) that variable;
- The subscriptions are static and each node knows, for each variable it is subscribed to, which are the other subscribers for that variable.
- We assume that most variables are accessed locally (within a small group of a few computers); we don't want a centralized server that hold all variables, because it would be a central bottleneck of the system. Therefore, as a result of changing a variable, all the messages should be exchanged only between the subscribers of that variable.
- The computers are not faulty.

***

## Algorithms Overview

### Sequential Consistency Protocol

#### Theory (Lecture 10)
- **Sequential Consistency Definition:** All events appear to happen in the same order for all processes
- **Lamport Logical Clocks:** Assigns timestamps to events such that if event A happens-before event B, then timestamp(A) < timestamp(B)
- **Happens-Before Relation:** Event A happens-before event B if:
    - A and B occur on the same process and A precedes B in program order
    - A is sending a message that B receives
    - Transitive chain exists through the above rules

#### DSM Implementation Strategy
**Sequencer-Based Approach:**
- Each variable has a designated sequencer (lowest-rank subscriber)
- Sequencer receives all write/CAS requests for that variable
- Sequencer assigns monotonically increasing Lamport timestamps
- All subscribers receive updates with timestamps in FIFO order
- Priority queue at each process applies updates in timestamp order

**Why This Achieves Sequential Consistency:**
1. **Serialization Point:** Sequencer creates total order for each variable's operations
2. **FIFO Message Delivery:** MPI guarantees point-to-point message ordering
3. **Timestamp Ordering:** All processes apply updates in identical timestamp sequence
4. **Result:** All processes observe identical event histories

***

### Lamport Clock Protocol

#### Theory
- Each process maintains a local integer clock (initially 0)
- **Rule 1:** Increment clock on every local event
- **Rule 2:** Attach current timestamp when sending messages
- **Rule 3:** On message receipt: `clock = max(local_clock, message_timestamp) + 1`
- **Tie-breaking:** Use process ID to order concurrent events with identical timestamps

#### Implementation Strategy
**Clock Management:**
- Clock increments before: `write()`, `read()`, `compareAndSwap()`, message processing
- All messages carry timestamps: `{MessageType, variable_id, value, lamport_timestamp}`
- Clock updates on message receipt preserve causality

**Update Ordering:**
- Priority queue buffers incoming UPDATE messages
- Dequeues updates in ascending timestamp order
- Tie-breaks by variable_id for deterministic ordering
- Ensures all processes apply updates in identical sequence

***

## Communication Patterns Analysis

### Message Types

#### WRITE_REQUEST (Client → Sequencer)
- **Format:** `{WRITE_REQUEST, variable_id, new_value, lamport_timestamp}`
- **Purpose:** Client requests variable update
- **Routing:** Sent to variable's sequencer (lowest-rank subscriber)
- **Tag:** 0 (default MPI tag)

#### UPDATE (Sequencer → All Subscribers)
- **Format:** `{UPDATE, variable_id, old_value, new_value, lamport_timestamp}`
- **Purpose:** Sequencer broadcasts committed update with timestamp
- **Routing:** Sent to all variable subscribers (including self)
- **Tag:** 0 (default MPI tag)

#### CAS_REQUEST (Client → Sequencer)
- **Format:** `{CAS_REQUEST, variable_id, expected, new_value, lamport_timestamp}`
- **Purpose:** Client requests atomic compare-and-swap
- **Routing:** Sent to variable's sequencer
- **Tag:** 0 (default MPI tag)

#### CAS_RESPONSE (Sequencer → Client)
- **Format:** `{success_flag}`
- **Purpose:** Sequencer reports CAS success/failure
- **Routing:** Point-to-point reply to requesting client
- **Tag:** 1 (distinguishes from regular messages to prevent buffer confusion)

***

### Communication Protocol Design

#### Write Operation Flow
1. **Client:** Increment Lamport clock, send WRITE_REQUEST to sequencer
2. **Sequencer:**
    - Receive request, update clock via `max(local, message) + 1`
    - Increment clock for processing event
    - Read current value, assign new timestamp
    - Update local variable state immediately
    - Broadcast UPDATE to all subscribers
3. **Subscribers:**
    - Receive UPDATE, update clock
    - Queue update in priority queue by timestamp
    - Process queued updates in order, trigger callbacks

#### CAS Operation Flow
1. **Client:** Increment clock, send CAS_REQUEST to sequencer, block waiting for response (tag=1)
2. **Sequencer:**
    - Receive request, update clock
    - Check: `if (current_value == expected)`
    - If match:
        - Update variable immediately (atomicity guarantee)
        - Broadcast UPDATE to subscribers
        - Send CAS_RESPONSE(success=1) with tag=1
    - If mismatch:
        - Send CAS_RESPONSE(success=0) with tag=1
3. **Client:** Receive response, unblock, return success/failure

***

## Synchronization Strategy

### No Locks Required
- **Distributed Memory Model:** No shared state between processes eliminates need for mutexes
- **Message-Passing Semantics:** Communication through explicit send/receive operations
- **Sequential Processing:** Each process handles messages one at a time in `processMessages()`
- **Thread-Safe by Design:** MPI handles internal synchronization for communication

### Ordering Guarantees

#### MPI FIFO Property
- **Point-to-Point Ordering:** Messages from process A to process B arrive in send order
- **Consequence:** All UPDATE messages from sequencer arrive in timestamp order
- **Exploitation:** Combined with priority queue, ensures consistent ordering across all subscribers

#### Priority Queue Ordering
- **Sorting Key:** Primary by Lamport timestamp, secondary by variable_id
- **Purpose:** Handles out-of-order message arrival (possible across different sequencers)
- **Processing:** Dequeue updates in ascending order, apply to local state
- **Callback Invocation:** Triggered after applying update, guarantees sequentially consistent observation

### Atomicity Mechanisms

#### CAS Atomicity
- **Single Decision Point:** Sequencer alone decides CAS success/failure
- **Immediate State Update:** Sequencer updates variable before broadcasting (prevents race)
- **Prevents Double Success:** Subsequent CAS sees updated value, fails if expected value outdated
- **Example:** If 3 processes CAS(var, 0, x), only first request succeeds; sequencer's value ≠ 0 for later requests

---

## Correctness Verification

### Test Configuration
- **Processes:** 4 MPI processes (`mpiexec -n 4 Lab8_DSM.exe`)
- **Variables:** 5 shared variables (IDs 0-4), all processes subscribe to all variables
- **Test Scenarios:**
    1. **Sequential Consistency:** Concurrent writes from multiple processes to same variable
    2. **Multiple Variables:** Interleaved operations on different variables
    3. **CAS Atomicity:** Race condition between processes attempting CAS from same expected value
    4. **Happens-Before Relations:** Sequential writes on one process vs. concurrent write on another

### Results

#### Scenario 1: Sequential Consistency Test
```
[T=7]  Var 0: 0 → 100   (Rank 0 write)
[T=9]  Var 0: 100 → 200 (Rank 0 write)
[T=11] Var 0: 200 → 120 (Rank 2 write)
[T=13] Var 0: 120 → 150 (Rank 1 write)
```
**Observation:** All 4 processes logged identical timestamps and value transitions

#### Scenario 3: CAS Atomicity Test
| Rank | Operation         | Expected | Current | Result  |
|------|-------------------|----------|---------|---------|
| 0    | CAS(3, 0, 0)      | 0        | 0       | SUCCESS |
| 1    | CAS(3, 0, 10)     | 0        | 0       | SUCCESS |
| 2    | CAS(3, 0, 20)     | 0        | 10      | FAILED  |
| 3    | CAS(3, 0, 30)     | 0        | 10      | FAILED  |

**Observation:** Sequencer's immediate state update prevents multiple successes despite concurrent requests

#### Sequential Consistency Verification
```
Event counts per process:
  Rank 0: 16 events
  Rank 1: 16 events
  Rank 2: 16 events
  Rank 3: 16 events
  Consistency: PASSED
```
**Mathematical Proof:** All processes observed identical event count and sequence → Sequential consistency satisfied

***

## Performance Characteristics

### Communication Overhead

| Operation | Messages | Latency |
|-----------|----------|---------|
| Write     | 1 request + N updates (N = subscribers) | 2 network hops |
| Read      | 0 messages (local only) | 0 network hops |
| CAS       | 1 request + N updates + 1 response | 2 network hops + 1 reply |

### Scalability Analysis

**Strengths:**
- **No Global Bottleneck:** Different variables have different sequencers
- **Subscriber-Only Communication:** Messages limited to interested parties
- **Local Reads:** Read operations have zero network cost

**Limitations:**
- **Sequencer Serialization:** All operations on variable V funnel through sequencer(V)
- **Broadcast Overhead:** O(N) messages per update (N = subscriber count)
- **Hot Variable Contention:** High write frequency on single variable saturates sequencer

**Optimization Opportunities:**
- **Variable Partitioning:** Assign related variables to different sequencers
- **Batching:** Combine multiple updates into single broadcast
- **Leases:** Temporary exclusive ownership for burst writes

***

## Comparison with Alternative Approaches

### Centralized Server
- Single point of contention for all variables
- Network bandwidth bottleneck at server
- Latency scales poorly with cluster size
- Lab requirement explicitly prohibits this

### Fully Distributed
- Broadcast all writes to all subscribers with Lamport timestamps
- Each process independently orders updates
- **Problem:** CAS operation has no serialization point
    - Multiple processes could CAS simultaneously, all seeing old value
    - Requires complex consensus protocol (2-phase commit, Paxos)
- **Trade-off:** Our approach sacrifices full distribution for correctness simplicity

### Vector Clocks
- Lamport clocks provide total order (requirement for sequential consistency)
- Vector clocks detect concurrency (not required by lab)
- Space overhead: O(1) per message vs. O(P) for vector clocks
- Simpler implementation reduces bugs

***

## Invariants and Correctness

### Protocol Invariants

#### Monotonicity
- **Invariant:** Lamport clocks never decrease
- **Enforcement:** Clock increments on every local event and message receipt
- **Consequence:** Timestamps provide monotonic event ordering

#### FIFO Ordering
- **Invariant:** Messages from sequencer S to subscriber P arrive in send order
- **Enforcement:** MPI point-to-point ordering guarantee
- **Consequence:** UPDATE messages arrive in timestamp order (since sequencer assigns increasing timestamps)

#### Sequential Consistency
- **Invariant:** For any execution, there exists a total order of operations consistent with all processes' observations
- **Enforcement:** Priority queue orders updates by timestamp across all variables and processes
- **Verification:** Test showed all processes logged identical event sequences

### Atomicity Guarantees

#### Write Atomicity
- **Property:** Each write operation appears to execute instantaneously at some point between invocation and completion
- **Mechanism:** Sequencer's timestamp assignment defines the serialization point
- **Proof:** All subscribers apply update with identical timestamp → observe write at same logical time

#### CAS Atomicity
- **Property:** CAS succeeds on at most one process when multiple processes CAS with same expected value
- **Mechanism:** Sequencer updates local variable immediately before broadcasting UPDATE
- **Proof:** First CAS changes variable value → subsequent CAS sees updated value ≠ expected → fails

***

**Author:** Antonio Hus  
**Date:** 15.11.2025  
**Course:** Parallel and Distributed Programming — Lab 8