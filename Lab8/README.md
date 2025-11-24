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
***

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

***

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

***

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

***

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

***

## Synchronization Strategy

### Single-Threaded Per Process, No Locks
- Each MPI process runs single-threaded and interacts with the DSM via synchronous calls (`write`, `compareAndSwap`, `read`).
- All shared DSM state is local to each process (using vectors/arrays).
- MPI’s internal synchronization provides delivery/order guarantees for messages between processes.

### Ordering Guarantees

- Lamport clocks ensure that all causal relationships respect the timestamp order.
- The priority queue ordered by `(timestamp, sender, msg_id)` implements a deterministic tie-break among concurrent operations.
- Per-variable ACK sets ensure that no process delivers a message before all subscribers have seen it, guaranteeing “uniform total order” delivery for each variable within its subscriber group.

***

## Correctness Verification

### Test Configuration

- **Processes:** 4 MPI processes (`mpiexec -n 4 Lab8_DSM.exe`)
- **Variables:** 5 integer variables (IDs 0–4)
    - Vars 0,1,3,4: All ranks subscribe.
    - Var 2: Only ranks {0,2,3} subscribe — to illustrate local groups.
- **Scenarios Tested:**
    1. Sequential consistency with concurrent writes on var 0.
    2. Multi-variable, mixed group (var 1 global, var 2 partial group).
    3. CAS atomicity under contention on var 3.
    4. Lamport happens-before with staggered writes to var 4.

### Example Event Orders (Observed)

- **Var 0:** All ranks, consistent global order (by Lamport timestamp).
- **Var 2:** Only ranks 0/2/3 observe callbacks for var 2; others (1) never see/trigger events for it.
- **Var 3:** Only one CAS success globally.
- **Sequential Consistency:** Checked by gathering callback counts from all ranks for global vars.

Sample output:
```
Event counts per process (vars 0,1,3,4 only):
Rank 0: 12 events
Rank 1: 12 events
Rank 2: 12 events
Rank 3: 12 events
Consistency: PASSED
```

***

## Performance Characteristics

- For subscription group size S, each update requires O(S²) messages due to ACK multicasting.
- Reads are always local/stateful (no message).
- No process ever observes changes for variables it is not subscribed to.

***

## Key Invariants and Guarantees

- For a given variable, all subscribing processes deliver events in the same global total order.
- Only subscribers of a variable can write to or receive updates for it.
- The callback order for each variable is always the same across its subscriber group.
- The protocol remains decentralized with only peer-to-peer coordination within static subscription groups.

***

**Author:** Antonio Hus  
**Date:** 16.11.2025  
**Course:** Parallel and Distributed Programming — Lab 8