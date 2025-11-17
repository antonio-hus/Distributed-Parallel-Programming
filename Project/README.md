# Parallel & Distributed Programming — Project Documentation

## Problem Statement

### Common Requirements

Each student or team of 2 students will take one project. It is ok to take a theme that is not listed below (but check with the lab advisor before starting).
Each project will have 2 implementations: one with "regular" threads or tasks/futures, and one distributed (possibly, but not required, using MPI). A third implementation, using OpenCL or CUDA, can be made for a bonus.

The documentation will describe:
- the algorithms,
- the synchronization used in the parallelized variants,
- the performance measurements

### Theme: School Timetabling by Exhaustive Search

Theme: **Generate a school timetable through exhaustive search**.

The project generates a weekly school/university timetable by exhaustive search over all valid assignments of teaching activities to time slots and rooms. The search enforces a rich set of hard constraints (no overlaps, qualification, travel time, workload) and optimizes a soft-constraint score (late slots, gaps, building locality).

***

## Problem Model

Construct a weekly timetable for a small university-like setting, given:

- A fixed weekly time grid (Monday–Friday, 08:00–20:00, 2-hour slots).
- A set of buildings with rooms and travel times between buildings.
- A set of subjects, each requiring course/seminar/lab slots per week.
- A set of professors, each qualified to teach specific subjects and activity types.
- A set of student groups, with the subjects they attend.

The core data model:

### Time Model

- **Days:** Monday–Friday (5 days).
- **Slots per day:** 6 slots of 2 hours each:
    - Slot 0: 08:00–10:00
    - Slot 1: 10:00–12:00
    - Slot 2: 12:00–14:00
    - Slot 3: 14:00–16:00
    - Slot 4: 16:00–18:00
    - Slot 5: 18:00–20:00
- **Time slot:** `(day, slot)`, where `day ∈ {0..4}`, `slot ∈ {0..5}`.

### Buildings and Rooms

- **Building**
    - `id`
    - `name`
    - Appears in the `travelTime[buildingA][buildingB]` matrix (minutes between buildings).

- **Room**
    - `id`
    - `buildingId`
    - `name` (e.g. `"C301"`)
    - `capacity` (optional for extra constraints)
    - `type ∈ {COURSE, SEMINAR, LAB}` (suitability for activity types)

### Subjects and Activities

- **Subject**
    - `id`
    - `name`
    - Weekly requirements in 2-hour slots:
        - `courseSlots`
        - `seminarSlots`
        - `labSlots`

From subjects, groups, and professor qualifications, the project generates **activities**:

- **Activity**
    - `id`
    - `subjectId`
    - `type ∈ {COURSE, SEMINAR, LAB}`
    - `profId` (professor teaching)
    - `groupIds` (list of student groups attending)
    - `durationSlots = 1` (each activity = one 2-hour slot)

**Course activities:**

- For each subject with `courseSlots > 0`, each course activity is shared by **all groups** that take that subject.
- At a course time, all those groups must attend and must be free (no overlap with other activities).
- Courses are global joint sessions for that subject’s groups.

### Professors

- **Professor**
    - `id`
    - `name`
    - `canTeachCourse: set<subjectId>`
    - `canTeachSeminar: set<subjectId>`
    - `canTeachLab: set<subjectId>`

Only valid `(subject, type, prof)` combinations are turned into `Activity` instances.

### Student Groups

- **Group**
    - `id`
    - `name`
    - `subjects: set<subjectId>` they attend.

For each `(group, subject)` and each required slot type:

- `courseSlots`: one course activity per slot, attended by all groups of that subject.
- `seminarSlots` and `labSlots`: group-specific activities.

***

## Timetable Representation

A **timetable** is a complete mapping of every `Activity` to:

- A time slot `(day, slot)`
- A `roomId` (or `roomIndex` into the rooms array)

Formally:

```text
ActivityId -> (day, slot, roomId)
```

The search algorithm manipulates **partial timetables**, represented as:

```cpp
struct Placement {
    int activityId;  // -1 if unused
    int day;
    int slot;
    int roomIndex;
};
```

A `std::vector<Placement>` is indexed by `activityId`. During search, some entries are still unused (`activityId == -1`).

A `TimetableState` object maintains incremental schedules for rooms, groups, and professors, plus professor workloads.

***

## Hard Constraints

A timetable is **valid** if all these constraints are satisfied:

1. **Time Window Constraint**
    - All activities must be assigned to days `0..4` and slots `0..5` (08:00–20:00).
    - Enforced structurally by iterating only valid `(day, slot)` pairs.

2. **Room Occupancy Constraint**
    - At most one activity per `(day, slot, room)`.
    - Implementation:
        - `roomSchedule[room][day][slot]` is either `kNone` (empty) or an `activityId`.
        - `place` rejects placements where the room is already occupied.

3. **Group No-Overlap Constraint**
    - A group cannot attend two activities at the same time.
    - Implementation:
        - `groupSchedule[group][day][slot]` must be `kNone` for all groups of the activity; otherwise `place` fails.

4. **Professor No-Overlap Constraint**
    - A professor cannot teach two activities at the same time.
    - Implementation:
        - `profSchedule[prof][day][slot]` must be `kNone`; otherwise `place` fails.

5. **Course = All-Groups Constraint**
    - For a course activity:
        - All groups taking the subject must be free at `(day, slot)`.
        - No other activity overlaps for those groups at that time.
    - Implementation:
        - When placing a course `(subjectId, COURSE)`:
            - Iterate all groups in `activity.groupIds`.
            - Check their `groupSchedule[group][day][slot]` entries.
            - On success, mark the course in all those group schedules.

6. **Professor Qualification Constraint**
    - Only professors allowed to teach `(subject, type)` may be assigned.
    - Implementation:
        - Enforced at activity generation; solvers only see valid `Activity` instances.

7. **Room Type Constraint**
    - Activity type must match room type:
        - `COURSE` → course rooms.
        - `SEMINAR` → seminar rooms.
        - `LAB` → lab rooms.
    - Implementation:
        - Before calling `place`, the solver checks `room.type` vs `activity.type`.

8. **Travel Time Constraint (Groups and Professors)**
    - No impossible back-to-back jumps between buildings:
        - If a group or professor has activities in consecutive slots in a day, the travel time between the two buildings must fit into a fixed break (e.g. 10 minutes).
    - Implementation:
        - When placing an activity in `roomBuilding` at `(day, slot)`:
            - Check `slot-1` and `slot+1` in `groupSchedule` and `profSchedule`.
            - If an adjacent activity exists, compare building indices via `travelTime`.
            - Reject placements where `travelTime[A][B]` exceeds allowed break.

9. **Professor Weekly Workload Constraints**
    - Lower bound: at least **20 hours** (10 activities) per week.
    - Upper bound: at most **40 hours** (20 activities) per week.
    - Implementation:
        - `profHours[prof]` stores current hours (each activity adds 2 hours).
        - Incremental upper bound:
            - During `place`, reject assignments that would exceed 40 hours.
        - Final global bound:
            - After a full timetable is built, `checkFinalWorkloadBounds()` verifies `20 ≤ totalHours ≤ 40` for all professors.

These bounds can be used for additional pruning (see below).

***

## Soft Constraints and Scoring

Soft constraints define a **score**; lower score is better. A timetable’s score is a non-negative integer built from:

1. **Avoid Late Slots**
    - Activities in slots 4 and 5 (16:00–20:00) are penalized (e.g. +1 per activity).

2. **Compact Days for Groups**
    - For each group and day:
        - Let `first` = first occupied slot, `last` = last occupied slot.
        - Count idle slots between `first` and `last`.
        - Each gap slot adds 1 penalty.

3. **Compact Days for Professors**
    - Same as above, but for each professor and day.

4. **Building Locality**
    - For each group and professor:
        - For each day, compute the set of buildings used.
        - Using more than 2 distinct buildings that day incurs a penalty for each extra building.

All CPU solvers share the same scoring function. The OpenCL implementation implements the equivalent logic inside kernels.

***

## Algorithms

### Sequential Backtracking Algorithm

#### Theory

The search space (all mappings of activities to `(day, slot, room)`) is enormous, but practical instances can be solved by:

- Enforcing all hard constraints incrementally.
- Ordering activities to expose conflicts early.
- Optionally using workload bounds for pruning.
- Exploring with depth-first search (DFS).

#### State Representation

- `TimetableState` holds:
    - `roomSchedule[room][day][slot]`.
    - `groupSchedule[group][day][slot]`.
    - `profSchedule[prof][day][slot]`.
    - `profHours[prof]` (current workload in hours).

- `std::vector<Placement> placements` indexed by `activityId`.

- `orderedActivities`:
    - A copy of all activities sorted by a heuristic:
        - Courses first.
        - For equal type, higher `groupIds.size()` first.

#### Depth-First Search

Pseudo-structure:

```cpp
void backtrack(int depth) {
    if (solutionsFound >= maxSolutions) return;

    if (depth == orderedActivities.size()) {
        if (!state.checkFinalWorkloadBounds()) return;

        int score = computeScore(placements);
        if (score < bestScore) {
            bestScore = score;
            best = { placements, score };
        }
        ++solutionsFound;
        return;
    }

    const Activity& act = orderedActivities[depth];

    for (int day = 0; day < DAYS; ++day)
        for (int slot = 0; slot < SLOTS_PER_DAY; ++slot)
            for (int roomIdx = 0; roomIdx < numRooms; ++roomIdx) {

                if (!roomTypeCompatible(act, roomIdx)) continue;

                if (state.place(act, day, slot, roomIdx)) {
                    placements[act.id] = { act.id, day, slot, roomIdx };
                    backtrack(depth + 1);
                    state.undo(act, day, slot, roomIdx);
                }
            }
}
```

#### Pruning

- Local checks in `place` reject:
    - Room, group, prof overlaps.
    - Course all-groups constraints.
    - Travel-time violations.
    - Professor workload upper bound.

- Global check:
    - After all activities are placed, `checkFinalWorkloadBounds()` enforces the minimum workload.

- Potential extra pruning:
    - For each professor, if `profHours[prof] > 40`, prune immediately.
    - If `profHours[prof] + remainingAssignableHours < 20`, prune.
    - Similarly, for groups, if remaining free slots are insufficient to place remaining activities, prune.

The sequential solver acts as the baseline for all performance comparisons.

***

### Shared-Memory Parallel Implementation (Threads)

The threaded solver parallelizes backtracking by splitting the search tree at a **frontier depth** `D`.

#### Decomposition

1. **Frontier generation (sequential):**
    - Starting from an empty `TimetableState`, run DFS down to depth `D`.
    - For each leaf at depth `D` (or when all activities are placed earlier), store a `PartialState`:
        - `TimetableState state` (snapshot).
        - `std::vector<Placement> placements`.
        - `int depth` (next activity index).

2. **Parallel exploration:**
    - Create `numThreads` worker threads.
    - Store all `PartialState` objects in a shared vector `frontier`.
    - Use a shared index `frontierIndex` to distribute work:
        - Each thread:
            - Atomically claims `frontier[frontierIndex++]`.
            - Copies its `state` and `placements`.
            - Runs DFS from `depth` until the subtree is fully explored or `maxSolutions` is reached.

This yields coarse-grained tasks: each task corresponds to exploring the subtree under one partial timetable.

#### Execution

Each worker executes:

```cpp
void workerLoop() {
    while (true) {
        PartialState* ps = claimNextPartialState();
        if (!ps) break;
        backtrackFromPartial(*ps);
    }
}
```

`backtrackFromPartial` is structurally identical to the sequential DFS, starting from the recorded `depth` and using the copied `TimetableState` and placements.

#### Synchronization

- **Frontier distribution:**
    - `frontierIndex` is protected by a mutex (`queueMutex`).
    - Ensures each `PartialState` is processed exactly once.

- **Shared best solution:**
    - Shared global variables:
        - `TimetableSolution best`.
        - `int bestScore`.
        - `int solutionsFound`.
    - Updates protected by `bestMutex`:
        - After a full timetable is found and passes `checkFinalWorkloadBounds()`, the worker computes the score and under the lock:
            - If `score < bestScore`, update `best` and `bestScore`.
            - Increment `solutionsFound`.

- **State isolation:**
    - Each worker has its own local copy of `TimetableState` and placement vector; there is no shared mutable search state.
    - Only task distribution and best-solution variables are shared.

***

### Distributed Implementation (MPI)

The MPI-based solver extends parallelism across processes using a **hybrid** approach: MPI ranks + internal threads.

There are two conceptual designs; this project uses the **multi-start** design.

#### Hybrid Multi-Start Design

- Each MPI rank:
    - Holds the same `ProblemInstance` in memory.
    - Creates a local copy and **randomly shuffles** the activity order with a rank-dependent RNG seed.
    - Runs the threaded solver:
        - With its own `maxSolutions`.
        - With `numThreads` threads.
        - With a chosen `frontierDepth`.

- Because the activity order and thus the search order differ per rank, the ranks explore different parts of the search space and are likely to find good solutions at different times.

#### Global Reduction of Best Score

- After local search, each rank has:
    - Either a local best solution with score `localScore`.
    - Or no solution, represented by `localScore = +∞`.

- Using MPI:
    - A global `MPI_Allreduce` with `MPI_MIN` finds `globalBestScore`.
    - A second reduction identifies the **winning rank** (the rank that achieved `globalBestScore`).

#### Solution Transfer

- If rank 0 is the winner:
    - It already has the best timetable and simply prints or returns it.

- If a non-zero rank is the winner:
    - The winner serializes its placements into a flat integer buffer:
        - For each `Placement`, send `(activityId, day, slot, roomIndex)`.
    - It sends the buffer length and data to rank 0.
    - Rank 0 receives the data, reconstructs the timetable, and presents it as the global best.

#### Synchronization

- Between ranks:
    - Only MPI collectives (`MPI_Allreduce`) and point-to-point (`MPI_Send`/`MPI_Recv`) are used.
    - No shared memory or locks, processes are independent.

- Within each rank:
    - The same thread-level synchronization as in the threaded solver (mutex-protected best solution, frontier index).

The result is a **hybrid MPI + threads** solver that scales across cores (threads) and nodes/processes (MPI).

***

## OpenCL Implementation (Bonus)

### Motivation

Exhaustive backtracking is highly irregular, making it a poor fit for GPUs as a whole-program algorithm. However, the **scoring of many complete timetables** is data-parallel:

- Each candidate timetable is independent.
- The score function is identical and does not branch heavily on a per-candidate basis.

Therefore, the project uses OpenCL to accelerate **batched scoring** of complete timetables, while keeping backtracking and hard constraints on the CPU.

### CPU–GPU Work Split

- **CPU:**
    - Runs DFS with full `TimetableState` enforcement of hard constraints.
    - Every time a complete timetable is produced, append its placements to a batch.
    - When the batch reaches `batchSize`, offload it to the GPU.

- **GPU (OpenCL):**
    - Receives batches of candidates.
    - For each candidate, reconstructs schedules and computes:
        - Late slot penalties.
        - Group day gaps.
        - Professor day gaps.
        - Building-locality penalties.
    - Returns:
        - `validFlags[i]` for structural checks.
        - `scores[i]` for soft constraints.

The host scans the results and updates the global best based on scores.

### Data Layout

Immutable data copied once to device:

- Buildings and `travelTime`.
- Rooms (building, type).
- Subjects, groups, professors.
- Activity metadata (subjectId, type, profId, groupIds).

Per-batch dynamic data:

- Flat encodings of candidate `Placement` lists, one per timetable.
- Output validity and score arrays.

### Kernel Types

1. **Evaluation Kernel**
    - One work-item per timetable.
    - Builds occupancy maps for groups/profs/rooms as needed.
    - Checks any remaining structural constraints (e.g., bounds).
    - Computes the score from soft constraints.

2. **Optional Reduction Kernel**
    - Finds the minimum score and its index in parallel.
    - In practice, this project does reduction on host for simplicity.

### Synchronization and Data Movement

- On device:
    - No locks; each work-item touches its own candidate data and output.

- Host–device:
    - Kernel launches are followed by blocking reads of `validFlags` and `scores` or by using events (future extension).
    - Batches are sized to amortize PCIe transfer cost.

This design demonstrates how to use OpenCL to accelerate a CPU-centric exhaustive search.

***

## Demo Instances and Scaling

The project defines several demo sizes:

```cpp
enum class DemoSize { XS, S, M, L, XL, XXL, XXXL };
ProblemInstance makeDemoInstance(DemoSize size);
```

Each size corresponds to a synthetic instance with different numbers of:

- Buildings and rooms.
- Professors and groups.
- Subjects and resulting activities.

Typical usage:

- `XS`, `S`: small instances for correctness testing and debugging.
- `M`: medium instance used for most demonstrations and comparisons.
- `L`, `XL`, `XXL`, `XXXL`: larger instances to stress-test the algorithms and highlight scalability of threads/MPI/OpenCL.

***

## Performance Measurements

### Example Test Configuration

For actual measurements, the following parameters are used (example):

- Time grid: 5 days × 6 slots.
- Scale (for medium size):
    - Buildings: ~2.
    - Rooms: ~6.
    - Groups: 3–4.
    - Subjects: 5–8.
    - Activities: 20–40.

Implementations tested:

- Sequential backtracking baseline.
- Threaded solver with 2, 4, 8, 16 threads.
- MPI+threads solver with 2, 4, 8 processes (each with a fixed number of threads).
- OpenCL solver with batch sizes such as 128, 512, 2048.

### Metrics

For each implementation and configuration:

- **Runtime**:
    - Wall-clock time to find the best timetable (or to explore a fixed number of solutions).

- **Speedup**
- **Scalability**:
    - How speedup evolves with:
        - Number of threads.
        - Number of MPI processes.
        - OpenCL batch size.

- **Search statistics** (if counters are enabled):
    - Number of complete timetables generated.
    - Total number of backtracking nodes visited.

- **OpenCL-specific metrics**:
    - Kernel execution time vs. data transfer time.
    - Impact of batch size on GPU utilization.

These measurements can be summarized in tables and plots. The discussion should explain:

- Why speedup deviates from linear (overhead, imbalance, contention).
- When MPI+threads is beneficial compared to threads only.
- When OpenCL scoring dominates, and how batch size affects performance.

| Demo size | Activities | Seq time [ms] | Threads time [ms] | MPI+Threads time [ms] | OpenCL time [ms] | Seq config (maxSolutions) | Threads config (threads, frontierDepth, maxSolutions) | MPI config (ranks, threads, maxSolutions) | OpenCL config (batchSize, maxSolutions) |
|-----------|------------|---------------|-------------------|-----------------------|------------------|---------------------------|-------------------------------------------------------|-------------------------------------------|---------------------------------------|
| XS        | 6          | 0.1423 ms     | 13.6796 ms        | 14.1764 ms            | 31.3419 ms       | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |
| S         | 6          | 0.2167 ms     | 16.3747 ms        | 14.5163 ms            | 34.4062 ms       | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |
| M         | 13         | 0.3576 ms     | 16.0067 ms        | 15.1763 ms            | 33.6328 ms       | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |
| L         | 30         | 0.5335 ms     | 65.4665 ms        | 18.3478 ms            | 34.5398 ms       | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |
| XL        | 45         | -             | 72.0952 ms        | 107.302 ms            | 33.0638 ms       | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |
| XXL       | 60         | -             | -                 | -                     | -                | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |
| XXXL      | 90          | -             | -                 | -                     | -                | 1                         | 16, 2, 1                                              | size=4, 16, 1                             | 512, 1                                |

***

## Invariants and Correctness

### Structural Invariants

- For each `(day, slot, room)`:
    - At most one activity is scheduled.
- For each `(group, day, slot)`:
    - At most one activity is scheduled.
- For each `(professor, day, slot)`:
    - At most one activity is scheduled.

### Course Invariants

- Every course activity for a subject is scheduled at times where:
    - All groups attending that subject are free.
    - Those groups are all assigned to that course activity at that time.
- No other group activities overlap with their subject’s course times.

### Travel Invariants

- For each group and professor:
    - Any pair of consecutive activities in the same day is either:
        - In the same building, or
        - In buildings whose travel time fits into the break between slots.

### Workload Invariants

- For each professor:
    - Total teaching hours satisfy `4 ≤ totalHours ≤ 40`.

### Parallel/Distributed Invariants

- **Shared-memory:**
    - No two threads modify the same `TimetableState` instance concurrently.
    - Global best solution and counters are updated under mutex protection.

- **MPI:**
    - Each rank handles its own search space (multi-start) and never shares mutable state with others.
    - Global reductions correctly identify the best score and corresponding rank.
    - When using a master–worker pattern (alternative design), each partial state is processed by exactly one worker.

- **OpenCL:**
    - Kernels are pure functions over input data, writing only to their designated output positions.
    - Host code validates flags and scores before accepting a candidate timetable.

Together, these invariants ensure that all implementations produce valid timetables, agree on constraint semantics, and differ only in how they explore the search space and how fast they reach good solutions.

***

**Author:** Antonio Hus  
**Date:** 17.11.2025  
**Course:** Parallel and Distributed Programming — Project (School Timetabling by Exhaustive Search)