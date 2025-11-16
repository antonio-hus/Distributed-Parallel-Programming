# Distributed Shared Memory with Lamport Clocks — Lab 8 Documentation

## Problem Statement

### Goal
The goal of this lab is to deal with the consistency issues of the distributed programming.

### Requirement
Implement a simple distributed shared memory (DSM) mechanism. The lab shall have a main program and a DSM library. There shall be a predefined number of communicating processes. The DSM mechanism shall provide a predefined number of integer variables residing on each of the processes. The DSM shall provide the following operations:
- Write a value to a variable (local or residing in another process);
- A callback informing the main program that a variable managed by the DSM has changed. All processes shall receive the same sequence of data change callbacks; it is not allowed that process P sees first a change on a variable A and then a change on a variable B, while another process Q sees the change on B first and the change on A second.
- A "compare and exchange" operation, that compares a variable with a given value and, if equal, it sets the variable to another given value. Be careful at the interaction with the previous requirement.

Notes:
- Only nodes that subscribe to a variable will receive notifications about changes of that variable, and only those nodes are allowed to change (set) that variable;
- The subscriptions are static and each node knows, for each variable it is subscribed to, which are the other subscribers for that variable.
- We assume that most variables are accessed locally (within a small group of a few computers); we don't want a centralized server that hold all variables, because it would be a central bottleneck of the system. Therefore, as a result of changing a variable, all the messages should be exchanged only between the subscribers of that variable.
- The computers are not faulty.
---

## Algorithms Overview

### Sequential Consistency Protocol

#### Theory (Lecture 10)
- Sequential consistency requires that the result of any execution be the same as if all operations were executed in some sequential order, and the operations of each individual process appear in this sequence in program order.
- Lamport logical clocks assign timestamps to events such that if event A happens-before event B, then timestamp(A) < timestamp(B).
- The happens-before relation A → B holds if:
  - A and B occur on the same process and A precedes B in the program order.
  - A is the sending of a message and B is the reception of that message.
  - The relation is transitively closed over the previous two.

#### Implementation Strategy: Fully Distributed Lamport Total-Order Multicast
The implementation uses a fully distributed totally ordered multicast based on Lamport clocks and acknowledgements:

- Each process maintains a Lamport clock and a priority queue of pending DSM operations, ordered by key `(timestamp, sender_rank, msg_id)`.
- To perform a write or CAS, a process:
  - Increments its Lamport clock and assigns this timestamp to the operation.
  - Multicasts the operation to all subscribers of that variable (including itself logically).
  - Inserts the operation into its own pending queue and records that it has “seen” this message.
- Upon receiving an operation, a process:
  - Updates its Lamport clock via `clock = max(clock, msg_timestamp) + 1`.
  - Inserts the message into its local priority queue.
  - Sends an acknowledgement (ACK) to the other subscribers of that variable.
- A message is delivered (applied to the DSM and triggering the callback) when:
  - It is at the head of the priority queue (smallest `(timestamp, sender, msg_id)`).
  - The process has received ACKs for that message from all subscribers of the variable.

This ensures that all subscribers deliver all operations in the same total order, giving sequential consistency for each variable among its subscribers, with no centralized sequencer.

---

### Lamport Clock Protocol

#### Theory
- Each process keeps a local clock `LC_p`, initially 0.
- Rules:
  - Increment the clock before every local event (write, CAS, read, sending).
  - Attach the current clock value as timestamp when sending a message.
  - On receive: set `LC_p = max(LC_p, timestamp_msg) + 1`.
- To totally order concurrent events with the same timestamp, use the pair `(timestamp, processID)` as the ordering key; if those are equal, a local per-sender `msg_id` breaks ties.

#### Implementation Details
- The DSM class maintains `int lamport_clock_` and increments it on:
  - `write`, `compareAndSwap`, `read` calls.
  - Reception of DSM messages (`UPDATE`, `CAS`, `ACK`).
- Each outgoing DSM message carries:
  - Message type (`UPDATE`, `CAS`, `ACK`).
  - `var_id`, `new_value`, and `expected` (for CAS).
  - `sender_rank`, `msg_id`, `timestamp`.
- The priority queue of pending messages is ordered by:
  - Primary key: Lamport `timestamp`.
  - Secondary: `sender_rank`.
  - Tertiary: `msg_id` (per-sender monotonically increasing).

---

## Communication Patterns

### Message Types

The implementation uses three message types, all represented as integer buffers in MPI.

#### UPDATE
- Format: `{UPDATE, var_id, new_value, expected=0, sender_rank, msg_id, timestamp}`.
- Purpose: Broadcast a state change for a variable after totally ordering it; used for simple writes.
- Routing: Multicast among all subscribers of `var_id`.

#### CAS
- Format: `{CAS, var_id, new_value, expected, sender_rank, msg_id, timestamp}`.
- Purpose: Represent an atomic compare-and-swap DSM operation, ordered like any other update.
- Routing: Multicast among all subscribers of `var_id`.

#### ACK
- Format: `{ACK, var_id, 0, 0, original_sender, msg_id, timestamp}`.
- Purpose: Signal that the sender has received and enqueued a particular DSM operation; used for the total-order delivery condition.
- Routing: Sent among subscribers of the same variable as the original message.

---

### Protocol Flows

#### Write Operation Flow
1. Initiator (process `p`):
   - Increments `lamport_clock_`.
   - Allocates a new `msg_id` and constructs an `UPDATE` message with `(timestamp, p, msg_id)`.
   - Inserts the message into its own priority queue and records that it has ACKed it.
   - Sends the message to all other subscribers of the variable.

2. Receiver:
   - Receives the `UPDATE`, updates its Lamport clock with `max(local, msg_timestamp) + 1`.
   - Inserts the message into its local priority queue.
   - Sends an `ACK` to the other subscribers (including the original sender).

3. Delivery:
   - Each process repeatedly checks the head of its priority queue.
   - When the message at the head has ACKs from all subscribers of that variable, it is delivered:
     - The local DSM value is updated.
     - The registered callback is invoked with `(var_id, old_value, new_value, timestamp)`.

Because all processes use the same ordering key and the same ACK condition, they deliver the same set of messages in the same total order.

#### CAS Operation Flow
1. Initiator:
   - Increments its Lamport clock, assigns a timestamp, and builds a `CAS` message `(var_id, expected, new_value, sender, msg_id, timestamp)`.
   - Inserts this message into its own priority queue and records its own ACK.
   - Multicasts the `CAS` message to all subscribers of `var_id`.

2. Receivers:
   - Handle the `CAS` message exactly like `UPDATE` at the transport level: update Lamport clock, enqueue, send ACKs.

3. Delivery:
   - When the `CAS` message is at the head of the queue and all subscribers have ACKed it, each process:
     - Checks its current value of `var_id`.
     - If `current_value == expected`, it sets the value to `new_value` and triggers the callback.
     - Otherwise, it leaves the value unchanged; no value-change callback is triggered.
   - The origin process waits internally until its own CAS message is delivered, then reads the result (`success` / `failure`) from a local map.

Because there is a single total order of all operations on each variable, at most one CAS with a given `expected` for a given variable can succeed globally, satisfying CAS atomicity.

---

## Synchronization Strategy

### Single-Threaded Per Process, No Locks
- Each MPI process runs single-threaded and interacts with the DSM via synchronous calls (`write`, `compareAndSwap`, `read`) plus periodic `processMessages` calls.
- All shared DSM state is purely local to each process (per-process `std::map<int,int>`), so there is no shared-memory data race across processes.
- MPI’s internal synchronization provides ordering and delivery guarantees for messages; no POSIX mutexes or C++ locks are required.

### Ordering Guarantees

- Lamport clocks ensure that all causal relationships respect the timestamp order.
- The priority queue ordered by `(timestamp, sender, msg_id)` implements a deterministic tie-break among concurrent operations.
- Per-variable ACK sets ensure that no process delivers a message before all subscribers have seen it, which is the typical “uniform total order” delivery condition.

---

## Correctness Verification

### Test Configuration

- Processes: 4 MPI processes, run as `mpiexec -n 4 Lab8_DSM.exe`.
- Variables: 5 integer variables with IDs 0–4.
  - Vars 0,1,3,4: All ranks subscribe.
  - Var 2: Only ranks {0,2,3} subscribe, to illustrate local groups and limited communication.
- Scenarios:
  1. Sequential consistency with concurrent writes on var 0.
  2. Multiple variables and different subscriber sets (vars 1 and 2).
  3. CAS atomicity under contention on var 3.
  4. Happens-before relations between writes on var 4.

### Observed Global Orders (Example Run)

- **Var 0 (Scenario 1):**  
  Global order (all ranks):  
  `0 → 150 → 100 → 120 → 200`  
  with Lamport timestamps `T = 5, 6, 6, 7`.

- **Var 1 (Scenario 2, global):**  
  Global order:  
  `0 → 15 → 12 → 10`  
  with timestamps `T = 21, 21, 23`.

- **Var 2 (Scenario 2, local group):**  
  Seen only by ranks 0,2,3:  
  `0 → 22 → 20`  
  with timestamps `T = 22, 24`; rank 1 sees no callbacks for var 2, as required.

- **Var 3 (Scenario 3, CAS):**  
  First, concurrent `CAS(3, 0, x)` from all ranks leads to a single winning CAS transitioning  
  `0 → 10` at timestamp `T = 36`, then later rank 0’s `CAS(3, 10, 999)` transitions  
  `10 → 999` at timestamp `T = 54`, both visible identically on all ranks.

- **Var 4 (Scenario 4):**  
  Writes from rank 1 and rank 0 produce the total order  
  `0 → 3 → 1 → 2`  
  with timestamps `T = 59, 60, 61` at all ranks.

### Sequential Consistency Check

The main program gathers the number of callbacks **only for vars 0,1,3,4** (those subscribed by all ranks) and checks that all processes report the same count.

Example run:

```
Event counts per process (vars 0,1,3,4 only):
Rank 0: 12 events
Rank 1: 12 events
Rank 2: 12 events
Rank 3: 12 events
Consistency: PASSED
```

Since all processes have identical callback counts and identical per-variable sequences, the DSM is sequentially consistent for the globally shared variables.

---

## Performance Characteristics

### Communication Cost

For a variable with subscriber set `S` of size `|S|`:

- **Write:**
  - 1 multicast `UPDATE` from the origin to `|S|-1` peers.
  - Each subscriber sends `ACK`s to the other subscribers, resulting in `O(|S|^2)` small messages in the worst case.

- **CAS:**
  - Same pattern as write (one `CAS` multicast + ACKs), plus a local wait at the origin until delivery, but no separate reply message is needed because success/failure is determined at delivery.

- **Read:**
  - Local only, zero network messages, as each process maintains a local copy of subscribed variables.

### Scalability Discussion

- **Strengths:**
  - No global centralized server; each variable is handled only by its subscriber group.
  - Reads are zero-latency network-wise.
  - Causality and total order are enforced purely by logical clocks and ACKs, independent of physical time.

- **Limitations:**
  - For a variable with many subscribers, the ACK-based total order produces `O(|S|^2)` message overhead per update.
  - Heavy write contention on a single variable still creates a logical bottleneck due to global ordering.

---

## Invariants and Correctness

### Logical Time Invariants

- Monotonicity: The Lamport clock of each process never decreases, because it is only incremented or updated to a maximum plus one on receive.
- Causality: If event A happens-before B, then the timestamp of A is strictly smaller than that of B.

### Total Order Invariants

- All processes use the same key `(timestamp, sender, msg_id)` to order pending messages, ensuring that any two messages will be delivered in the same relative order at all subscribers.
- A message is delivered only after all subscribers have ACKed it, preventing any process from “overtaking” another in terms of visible effects.

### DSM-Level Guarantees

- **Write Atomicity:** Each write appears to take effect at an instant defined by its position in the total order; all subscribers move from old to new value at that logical point.
- **CAS Atomicity:** For a given `(var, expected)` pair, at most one CAS succeeds across all processes, because only the first CAS in the total order can see the expected value.
- **Sequential Consistency:** For each variable and its subscribers, there exists a single total order of all operations such that each process’s local order is consistent with this global order, as validated by the callback sequences and event counts.

---

**Author:** Antonio Hus  
**Date:** 16.11.2025  
**Course:** Parallel and Distributed Programming — Lab 8