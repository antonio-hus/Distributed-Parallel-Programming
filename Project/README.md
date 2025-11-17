# Parallel & Distributed Programming — Project Documentation

## Problem Statement

### Common Requirements
Each student or team of 2 students will take one project. It is ok to take a theme that is not listed below (but check with the lab advisor before starting).

Each project will have 2 implementations: one with "regular" threads or tasks/futures, and one distributed (possibly, but not required, using MPI). A third implementation, using OpenCL or CUDA, can be made for a bonus.

The documentation will describe:
- The algorithms,
- The synchronization used in the parallelized variants,
- The performance measurements

### Theme
Generate a school timetable through exhaustive search.

## Project Overview
Construct a weekly timetable for a small university-like setting, given:

- A fixed weekly time grid (Monday–Friday, 08:00–20:00, 2-hour slots).
- A set of **buildings** with rooms and travel times between buildings.
- A set of **subjects**, each requiring a number of course/seminar/lab slots.
- A set of **professors**, each qualified to teach specific subjects and activity types.
- A set of **student groups**, with the list of subjects they attend.

Each project must provide:

1. A **sequential implementation** using exhaustive search / backtracking.
2. A **shared-memory parallel implementation** (threads/tasks/futures).
3. A **distributed implementation** (e.g., MPI), partitioning the search space across processes.
4. An implementation using **OpenCL**.

The documentation describes:
- The algorithms used (state representation, backtracking, pruning).
- The synchronization mechanisms in the parallel and distributed variants.
- Performance measurements and scalability.
- For OpenCL: the formulation of the problem as data-parallel kernels.

---

## Problem Model

### Time Model

- **Days:** Monday–Friday (5 days).
- **Slots per day:** 6 slots of 2 hours each:
    - Slot 0: 08:00–10:00
    - Slot 1: 10:00–12:00
    - Slot 2: 12:00–14:00
    - Slot 3: 14:00–16:00
    - Slot 4: 16:00–18:00
    - Slot 5: 18:00–20:00
- **Time slot:** represented as `(day, slot)`, where `day ∈ {0..4}`, `slot ∈ {0..5}`.

### Buildings and Rooms

- **Building**
    - `id`
    - List of **rooms** contained in this building.
    - Symmetric `travelTime[buildingA][buildingB]` in minutes.

- **Room**
    - `id`
    - `buildingId`
    - `capacity` (optional, can be used for extra constraints).
    - `type ∈ {COURSE, SEMINAR, LAB}` (room suitability).

### Subjects and Activities

- **Subject**
    - `id`
    - `name`
    - Weekly requirements (in number of 2-hour slots):
        - `courseSlots`
        - `seminarSlots`
        - `labSlots`

From subjects + groups + prof qualifications, the project generates **activities**:

- **Activity**
    - `id`
    - `subjectId`
    - `type ∈ {COURSE, SEMINAR, LAB}`
    - `groupId` (the student group attending)
    - `profId` (the professor teaching it)
    - `durationSlots = 1` (2 hours per activity)

**Course activities are special:**

- For each subject with `courseSlots > 0`, the course activities are shared by **all groups that take that subject**.
- At a course time, all those groups must be free and must attend the course.
- Nothing can overlap with a course for those groups—courses act as global sessions for the subject’s groups.

### Professors

- **Professor**
    - `id`
    - `name`
    - `canTeachCourse: set<subjectId>`
    - `canTeachSeminar: set<subjectId>`
    - `canTeachLab: set<subjectId>`

Only valid `(subject,type,prof)` combinations are turned into `Activity` instances.

### Student Groups

- **Group**
    - `id`
    - `name`
    - `subjects: set<subjectId>` they attend.

For each `(group,subject)` and each required slot type, activities are generated:

- `courseSlots`: one course activity per slot, attended jointly by all groups of that subject.
- `seminarSlots` and `labSlots`: group-specific activities.

---

## Timetable Representation

A **timetable** is a complete mapping of every `Activity` to:

- A time slot `(day, slot)`
- A `roomId`

Formally:

```
ActivityId -> (day, slot, roomId)
```

The search algorithm manipulates **partial timetables**, gradually assigning activities while ensuring constraints are respected.

---

## Hard Constraints

A timetable is **valid** if all the following constraints are satisfied:

1. **Time Window Constraint**
    - All activities must be assigned to slots 0–5 (08:00–20:00).
    - Enforced structurally by using only these slots.

2. **Room Occupancy Constraint**
    - At most one activity per `(day, slot, roomId)`.
    - Implementation: `roomSchedule[room][day][slot]` must be empty before placing an activity.

3. **Group No-Overlap Constraint**
    - A group cannot attend two activities at the same time.
    - For each `group`, `(day,slot)` can be assigned to at most one activity.
    - Implementation: `groupSchedule[group][day][slot]` check.

4. **Professor No-Overlap Constraint**
    - A professor cannot teach two activities at the same time.
    - For each `prof`, `(day,slot)` can be assigned to at most one activity.
    - Implementation: `profSchedule[prof][day][slot]` check.

5. **Course = All-Groups Constraint**
    - Every course activity for a subject is scheduled at `(day,slot)` where:
        - All groups taking that subject are free.
        - None of those groups has any other activity at that time.
    - Implementation:
        - When assigning a course `(subjectId, COURSE)`, iterate all its groups and ensure their `(day,slot)` is free.
        - Mark `(day,slot)` occupied by this course for all those groups.

6. **Professor Qualification Constraint**
    - Only professors allowed to teach `(subject,type)` may be assigned to that activity.
    - Enforced at generation time (activity list generation) or checked when assigning.

7. **Room Type Constraint**
    - Activity type must be compatible with room type:
        - `COURSE` → course/lecture rooms.
        - `LAB` → lab rooms.
        - `SEMINAR` → seminar or general-purpose rooms.
    - Implementation: check `room.type` against `activity.type`.

8. **Travel Time Constraint (Groups and Professors)**
    - No impossible back-to-back jumps between buildings:
        - If a group or professor has activities scheduled consecutively at `(day,slot)` in building `A` and `(day,slot+1)` in building `B`, then the travel time `travelTime[A][B]` must be ≤ allowed break (e.g., 10 minutes).
    - Implementation:
        - When placing an activity in `roomBuilding` at `(day,slot)`:
            - Check `slot-1` and `slot+1` for the same group/prof (if already scheduled).
            - If adjacent exists, enforce the travel-time bound.

9. **Professor Weekly Workload Constraints**
    - Lower bound: Each professor must have **at least 20 hours** of teaching per week.
        - With 2-hour slots, this means at least 10 activities.
    - Upper bound (optional but enforced in this project): Each professor must have **at most 40 hours** per week.
        - With 2-hour slots, at most 20 activities.
    - Implementation:
        - After a full timetable is built, for each professor:
            - Let `count` be the number of assigned activities.
            - Check `count * 2 ≥ 20` and `count * 2 ≤ 40`.

These workload bounds can also be used for pruning: if in a partial schedule a professor already exceeds 40 hours or cannot possibly reach 20 hours even if all remaining eligible activities are assigned, the branch can be pruned early.

---

## Soft Constraints (Optional)

These define a **score** for timetables; lower score is better:

1. **Avoid Late Slots**
    - Penalize activities scheduled in slots 4 and 5 (16:00–20:00).

2. **Compact Days for Groups**
    - Penalize large gaps within a group’s day (long idle periods between first and last activity).

3. **Compact Days for Professors**
    - Similar penalization for professors’ schedules.

4. **Building Locality**
    - Penalize days where a group/prof uses more than a certain number of different buildings.

The sequential exhaustive search can keep the **best** valid timetable according to this score; parallel and distributed variants do the same while exploring multiple branches concurrently.

---

## Algorithms Overview

### Sequential Backtracking Algorithm

#### Theory
- The full search space is huge, but constraints allow aggressive pruning.
- Depth-first search:
    - Activities are sorted in a heuristic order.
    - At depth `k`, the algorithm assigns `activities[k]` to each allowed `(day,slot,room)` position that satisfies local constraints.
    - If all activities are assigned:
        - Check global constraints (workload bounds).
        - Compute timetable score and update best solution if needed.

#### Implementation Strategy

**State Representation:**

- Arrays/maps:
    - `roomSchedule[room][day][slot]`
    - `groupSchedule[group][day][slot]`
    - `profSchedule[prof][day][slot]`
- Additional counters:
    - `profHours[prof]` (current workload in hours).

**Activity Ordering:**

- Courses first (higher fan-out due to all-group constraint).
- Then activities involving scarce resources (rooms/profs).
- This improves pruning effectiveness.

**Pruning:**

- On each assignment:
    - Check local constraints (room, group, prof collisions, travel time, room type).
- During recursion:
    - For each professor:
        - If `profHours[prof] > 40`, prune immediately.
        - If `profHours[prof] + remainingAssignableHours < 20`, prune (cannot reach minimum).
    - For each group:
        - If free slots remaining are insufficient to place its remaining activities, prune.

---

### Shared-Memory Parallel Implementation

#### Parallel Backtracking with Tasks

**Decomposition:**

- Choose a depth `D` as a frontier.
- Sequentially explore all assignments to the first `D` activities, generating a set of partial states.
- For each partial state, submit a **task** to a thread pool or use futures.

**Execution:**

- Each worker thread:
    - Takes one partial timetable.
    - Runs the same backtracking algorithm from depth `D` onward.
    - Reports:
        - Number of valid full timetables found (optional) and/or
        - Best timetable and its score.

**Synchronization:**

- Shared best solution:
    - Protected by a `std::mutex` or a lock-free compare-and-swap on score + index.
- Task distribution:
    - Shared queue protected by mutex/condition variable, or use a work-stealing scheduler.
- No sharing of partial timetables except as immutable snapshots.

---

### Distributed Implementation (MPI)

#### Master–Worker Backtracking

**Data Setup:**

- Rank 0 (master) reads instance data and broadcasts:
    - Buildings, rooms, travel times.
    - Subjects, groups, professors, and generated activities.
- All ranks hold identical copies of immutable data.

**Work Partitioning:**

- Master runs sequential backtracking down to depth `D`, producing many partial timetables (frontier nodes).
- Each partial timetable is serialized and sent to a worker process.

**Worker Process:**

- Receives a partial timetable and starting depth.
- Performs backtracking locally until all activities are assigned or pruned.
- Sends back:
    - Number of valid timetables.
    - Best timetable (lowest score) in its subtree, if any.

**Result Aggregation:**

- Master gathers results from all workers.
- Chooses the globally best timetable and computes summary statistics.

**Synchronization:**

- Master tracks outstanding tasks and workers.
- Workers terminate when they receive a “no more work” message.
- No shared memory between processes; all coordination is via MPI messages.

---

## OpenCL Implementation (Bonus)

### Motivation

While exhaustive backtracking is inherently irregular and branchy (not ideal for GPUs), OpenCL can still be used to accelerate **hot inner loops** and **constraint evaluation** when exploring multiple candidate assignments in parallel.

### OpenCL Formulation

#### Data Layout

Immutable data copied once to GPU buffers:

- `buildings` and `travelTime` matrix.
- `rooms` (type, building).
- `subjects`, `groups`, `professors`.
- Precomputed activity metadata (`subjectId`, `type`, `groupId`, `profId`).

Dynamic data:

- Arrays encoding candidate schedules or partial states to be scored.

#### Kernel Types

1. **Constraint Evaluation Kernel**
    - Input: flat encodings of multiple candidate timetables or partial timetables.
    - Each **work-item** validates one candidate:
        - Checks hard constraints:
            - Room occupancy.
            - Group/prof overlaps.
            - Course all-groups constraint.
            - Travel time constraints.
            - Professor workloads (20–40h).
        - Computes a boolean validity flag.
    - Each **work-item** also computes the soft-constraint score.

2. **Score Computation / Reduction Kernel**
    - Input: scores for valid candidates.
    - Goal: find minimum score and index of best candidate.
    - Classic parallel reduction pattern.

#### Usage in the Algorithm

- CPU side (host):
    - Generates batches of candidate assignments (partial or full timetables).
    - Offloads constraint checking and scoring to the GPU:
        - Sends the batch to the OpenCL kernel.
        - Reads back validity flags and scores.
    - Uses results to:
        - Filter out invalid candidates quickly.
        - Select promising candidates for deeper CPU backtracking.
- Possible strategy:
    - At some shallow depth, enumerate many partial assignments on CPU.
    - Pack them into a batch and send them to GPU for fast constraint pruning.
    - Only keep partial states that pass GPU checks and show good scores.

### Synchronization and Data Movement

- Host–device synchronization:
    - Blocking `clEnqueueReadBuffer` after kernel execution, or events for async.
- No device-side locks:
    - Each work-item operates independently on its own candidate.
- Performance trade-offs:
    - Amortize PCIe transfer costs by evaluating large batches at once.
    - Use struct-of-arrays (SoA) layout for coalesced memory access.

---

## Performance Measurements

### Test Configuration (Example)

- **Time grid:** 5 × 6 slots.
- **Scale:**
    - Buildings: 2.
    - Rooms: ~6.
    - Groups: 3 or 4.
    - Subjects: 5–8.
    - Activities: ~20–40 (courses + seminars + labs).
- **Implementations:**
    - Sequential backtracking (baseline).
    - Shared-memory threaded version (2, 4, 8 threads).
    - MPI version (2, 4, 8 processes).
    - Optional: OpenCL-accelerated constraint evaluation for batches.

### Metrics

- Runtime for each implementation and configuration.
- Speedup relative to sequential baseline.
- Number of explored states (nodes) in the search tree:
    - Helps explain why some variants scale better.
- For OpenCL:
    - Batch size vs kernel time.
    - Benefit from offloading constraint checking.

---

## Invariants and Correctness

### Structural Invariants

- For each `(day, slot, room)`:
    - At most one activity.
- For each `(group, day, slot)`:
    - At most one activity.
- For each `(prof, day, slot)`:
    - At most one activity.

### Course Invariants

- Every course activity for `(subjectId)` is scheduled at times where:
    - All groups attending that subject are free and assigned to that course.
- No group activity overlaps with its subject’s course times.

### Travel Invariants

- For each group and professor:
    - For any pair of consecutive slots in the same day, either:
        - Activities are in the same building, or
        - The travel time between buildings fits in the slot break.

### Workload Invariants

- For each professor:
    - `20 ≤ totalHours ≤ 40`.

### Parallel/Distributed Invariants

- Shared-memory:
    - No concurrent modification of the same partial timetable by multiple threads.
    - Global best timetable updates are atomic/mutex-protected.
- MPI:
    - Each partial state is processed by exactly one worker.
    - Master correctly aggregates counts and best scores.
- OpenCL:
    - Kernels are pure functions on input data; no side effects besides writing to output buffers.
    - Host code checks all kernel outputs before accepting a candidate as valid.

---

**Author:** Antonio Hus  
**Date:** 17.11.2025  
**Course:** Parallel and Distributed Programming — Project (School Timetabling by Exhaustive Search)