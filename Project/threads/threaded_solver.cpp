///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "threaded_solver.hpp"
#include <algorithm>
#include <functional>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Construct the threaded backtracking solver with given limits.
 *
 * Initializes configuration parameters; actual solver state is reset in solve().
 */
ThreadedBacktrackingSolver::ThreadedBacktrackingSolver(int maxSolutions, int numThreads, int frontierDepth)
        : maxSolutions_(maxSolutions),
          numThreads_(numThreads),
          frontierDepth_(frontierDepth) {}

/**
 * @brief Entry point for solving a timetable instance.
 *
 * Resets internal state, builds the search frontier sequentially, then launches
 * worker threads to explore each partial state in parallel. Returns the best
 * solution found or std::nullopt if no complete solution exists.
 */
std::optional<TimetableSolution> ThreadedBacktrackingSolver::solve(const ProblemInstance& inst) {
    // Keep a pointer to the instance for use in helpers.
    inst_ = &inst;
    // Precompute activity ordering to reduce branching.
    orderActivities(inst);

    // Reset shared state before starting a new search.
    best_.placements.clear();
    bestScore_ = std::numeric_limits<int>::max();
    solutionsFound_ = 0;
    frontier_.clear();
    frontierIndex_ = 0;

    // Build frontier of partial states up to frontierDepth_.
    buildFrontierSequential();

    // No frontier => no feasible partial assignment found.
    if (frontier_.empty()) {
        return std::nullopt;
    }

    // Launch worker threads, each consuming from frontier_.
    std::vector<std::thread> workers;
    for (int i = 0; i < numThreads_; ++i) {
        workers.emplace_back(&ThreadedBacktrackingSolver::workerLoop, this);
    }
    // Wait for all workers to finish.
    for (auto& t : workers) {
        t.join();
    }

    // If no complete solutions were found, report failure.
    if (solutionsFound_ == 0) {
        return std::nullopt;
    }
    // Return the best solution discovered.
    return best_;
}

/**
 * @brief Compute a heuristic ordering of activities for backtracking.
 *
 * Currently prioritizes course activities first, then sorts by number of
 * groups descending (more constrained activities earlier).
 */
void ThreadedBacktrackingSolver::orderActivities(const ProblemInstance& inst) {
    orderedActivities_ = inst.activities;

    std::sort(orderedActivities_.begin(), orderedActivities_.end(),
              [](const Activity& a, const Activity& b) {
                  // Put COURSES before other types.
                  if (a.type != b.type) {
                      return a.type == ActivityType::COURSE;
                  }
                  // For same type, schedule activities involving more groups first.
                  return a.groupIds.size() > b.groupIds.size();
              });
}

/**
 * @brief Build the frontier of partial states using a sequential DFS.
 *
 * Starts from an empty timetable state and explores assignments until the
 * configured frontier depth is reached, storing each partial assignment
 * as a PartialState in frontier_.
 */
void ThreadedBacktrackingSolver::buildFrontierSequential() {
    // Create an empty timetable state for this problem instance.
    TimetableState state(*inst_);

    // Preallocate placement array indexed by activity id.
    std::vector<Placement> placements(inst_->activities.size());
    for (auto& p : placements) {
        p.activityId = -1; // Mark as unused.
        p.day = 0;
        p.slot = 0;
        p.roomIndex = 0;
    }

    // Recursively grow partial states up to frontierDepth_.
    buildFrontierDFS(state, placements, 0);
}

/**
 * @brief Depth-first search that builds partial states for the frontier.
 *
 * At each depth, tries all valid (day, slot, room) placements for the current
 * activity. When depth reaches frontierDepth_ or all activities are assigned,
 * the current state is stored in frontier_.
 */
void ThreadedBacktrackingSolver::buildFrontierDFS(TimetableState& state, std::vector<Placement>& placements, int depth) {
    // Stop expanding and store this partial assignment as a frontier node.
    if (depth == frontierDepth_ || depth == (int)orderedActivities_.size()) {
        auto ps = std::make_unique<PartialState>(PartialState{state, placements, depth});
        frontier_.push_back(std::move(ps));
        return;
    }

    const Activity& act = orderedActivities_[depth];
    int numRooms = (int)inst_->rooms.size();

    // Try all feasible placements for the current activity.
    for (int day = 0; day < DAYS; ++day) {
        for (int slot = 0; slot < SLOTS_PER_DAY; ++slot) {
            for (int roomIdx = 0; roomIdx < numRooms; ++roomIdx) {
                const Room& room = inst_->rooms[roomIdx];

                // Enforce room type compatibility with activity type.
                if (act.type == ActivityType::COURSE && room.type != Room::Type::COURSE)
                    continue;
                if (act.type == ActivityType::SEMINAR && room.type != Room::Type::SEMINAR)
                    continue;
                if (act.type == ActivityType::LAB && room.type != Room::Type::LAB)
                    continue;

                // Try placing activity; if successful, go deeper in the search tree.
                if (state.place(act, day, slot, roomIdx)) {
                    Placement p{act.id, day, slot, roomIdx};
                    // Store placement by activity id so it can be reused/extended later.
                    placements[act.id] = p;
                    buildFrontierDFS(state, placements, depth + 1);
                    // Backtrack: undo the placement before trying the next option.
                    state.undo(act, day, slot, roomIdx);
                }
            }
        }
    }
}

/**
 * @brief Worker thread main loop.
 *
 * Each worker repeatedly claims the next available PartialState from the
 * shared frontier_ and continues backtracking from that state. Exits when
 * no more frontier nodes are left.
 */
void ThreadedBacktrackingSolver::workerLoop() {
    while (true) {
        std::unique_ptr<PartialState> ps;

        {
            // Claim the next frontier index atomically.
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (frontierIndex_ >= frontier_.size()) {
                // No more work available.
                break;
            }
            ps = std::move(frontier_[frontierIndex_++]);
        }

        // Continue the search from the claimed partial state.
        if (ps) {
            backtrackFromPartial(*ps);
        }
    }
}

/**
 * @brief Continue depth-first backtracking from a given partial state.
 *
 * Reconstructs the TimetableState and placements vector and then recursively
 * assigns remaining activities, updating the global best solution when a
 * better complete timetable is found.
 */
void ThreadedBacktrackingSolver::backtrackFromPartial(const PartialState& partial) {
    // Start from the snapshot in the partial state.
    TimetableState state = partial.state;
    std::vector<Placement> placements = partial.placements;
    int depth = partial.depth;

    // Recursive DFS lambda that continues from depth d.
    std::function<void(int)> dfs;
    dfs = [&](int d) {
        // Global stop condition: enough solutions found.
        if ((int)solutionsFound_ >= maxSolutions_) return;

        // All activities assigned: evaluate this complete timetable.
        if (d == (int)orderedActivities_.size()) {
            // Check final hard constraints that require full schedule context.
            if (!state.checkFinalWorkloadBounds()) {
                return;
            }

            int score = computeScore(placements);

            // Update global best in a thread-safe way.
            std::lock_guard<std::mutex> lock(bestMutex_);
            if (score < bestScore_) {
                bestScore_ = score;
                best_.placements = placements;
                best_.score = score;
            }
            ++solutionsFound_;
            return;
        }

        const Activity& act = orderedActivities_[d];
        int numRooms = (int)inst_->rooms.size();

        // Try every valid (day, slot, room) for the current activity.
        for (int day = 0; day < DAYS; ++day) {
            for (int slot = 0; slot < SLOTS_PER_DAY; ++slot) {
                for (int roomIdx = 0; roomIdx < numRooms; ++roomIdx) {
                    const Room& room = inst_->rooms[roomIdx];

                    // Enforce room/activity type compatibility.
                    if (act.type == ActivityType::COURSE && room.type != Room::Type::COURSE)
                        continue;
                    if (act.type == ActivityType::SEMINAR && room.type != Room::Type::SEMINAR)
                        continue;
                    if (act.type == ActivityType::LAB && room.type != Room::Type::LAB)
                        continue;

                    // Place, recurse, then undo (classic backtracking pattern).
                    if (state.place(act, day, slot, roomIdx)) {
                        Placement p{act.id, day, slot, roomIdx};
                        placements[act.id] = p;
                        dfs(d + 1);
                        state.undo(act, day, slot, roomIdx);
                    }
                }
            }
        }
    };

    // Resume DFS from the recorded depth.
    dfs(depth);
}

/**
 * @brief Compute soft-constraint score for a complete timetable.
 *
 * Lower scores are better. The score is composed of:
 *  - Late slot penalties for activities in late time slots.
 *  - Gap penalties for student groups (idle slots between first and last class).
 *  - Gap penalties for professors.
 *  - Building locality penalties for groups and professors using too many buildings per day.
 */
int ThreadedBacktrackingSolver::computeScore(const std::vector<Placement>& placements) const {
    const ProblemInstance& inst = *inst_;

    int score = 0;

    // LATE SLOT PENALTY
    // Penalize activities scheduled in later time slots (e.g., 16:00–20:00).
    for (const Placement& p : placements) {
        if (p.activityId < 0) continue;
        if (p.slot >= 4) {
            // slots 4 and 5 => late in the day.
            score += 1;
        }
    }

    // GROUP GAP PENALTY
    int numGroups = (int)inst.groups.size();

    // groupDaySlots[group][day][slot] = whether the group has an activity there.
    std::vector<std::vector<std::vector<bool>>> groupDaySlots(
            numGroups,
            std::vector<std::vector<bool>>(DAYS, std::vector<bool>(SLOTS_PER_DAY, false))
    );

    // Fill occupancy based on placements and group memberships.
    for (const Placement& p : placements) {
        if (p.activityId < 0) continue;
        const Activity& act = inst.activities[p.activityId];
        for (int gid : act.groupIds) {
            // Map group id to index.
            int gIdx = -1;
            for (int i = 0; i < numGroups; ++i) {
                if (inst.groups[i].id == gid) {
                    gIdx = i;
                    break;
                }
            }
            if (gIdx < 0) continue;
            groupDaySlots[gIdx][p.day][p.slot] = true;
        }
    }

    // For each group and day, penalize idle slots between first and last occupied slot.
    for (int g = 0; g < numGroups; ++g) {
        for (int d = 0; d < DAYS; ++d) {
            int first = -1;
            int last  = -1;
            for (int s = 0; s < SLOTS_PER_DAY; ++s) {
                if (groupDaySlots[g][d][s]) {
                    if (first == -1) first = s;
                    last = s;
                }
            }
            // No activities or only one activity => no gaps to penalize.
            if (first == -1 || last == -1 || first == last) {
                continue;
            }
            int gaps = 0;
            for (int s = first; s <= last; ++s) {
                if (!groupDaySlots[g][d][s]) {
                    gaps++;
                }
            }
            // Each idle slot counts as one penalty point.
            score += gaps;
        }
    }

    // PROFESSOR GAP PENALTY
    int numProfs = (int)inst.professors.size();

    // profDaySlots[prof][day][slot] = whether the professor teaches then.
    std::vector<std::vector<std::vector<bool>>> profDaySlots(
            numProfs,
            std::vector<std::vector<bool>>(DAYS, std::vector<bool>(SLOTS_PER_DAY, false))
    );

    // Fill occupancy per professor.
    for (const Placement& p : placements) {
        if (p.activityId < 0) continue;
        const Activity& act = inst.activities[p.activityId];

        int profIdx = -1;
        for (int i = 0; i < numProfs; ++i) {
            if (inst.professors[i].id == act.profId) {
                profIdx = i;
                break;
            }
        }
        if (profIdx < 0) continue;

        profDaySlots[profIdx][p.day][p.slot] = true;
    }

    // For each professor and day, penalize idle slots between first and last class.
    for (int pr = 0; pr < numProfs; ++pr) {
        for (int d = 0; d < DAYS; ++d) {
            int first = -1;
            int last  = -1;
            for (int s = 0; s < SLOTS_PER_DAY; ++s) {
                if (profDaySlots[pr][d][s]) {
                    if (first == -1) first = s;
                    last = s;
                }
            }
            if (first == -1 || last == -1 || first == last) {
                continue;
            }
            int gaps = 0;
            for (int s = first; s <= last; ++s) {
                if (!profDaySlots[pr][d][s]) {
                    gaps++;
                }
            }
            score += gaps;
        }
    }

    // BUILDING LOCALITY PENALTY
    // Penalize days where a group or professor uses more than two distinct buildings.

    // Helper to map room index to building index (or -1 if invalid).
    auto roomIndexToBuildingIndex = [&](int roomIndex) -> int {
        if (roomIndex < 0 || roomIndex >= (int)inst.rooms.size()) return -1;
        // In this model, buildingId is assumed to be a valid index.
        return inst.rooms[roomIndex].buildingId;
    };

    // Group-level building diversity per day.
    for (int g = 0; g < numGroups; ++g) {
        int groupId = inst.groups[g].id;
        for (int d = 0; d < DAYS; ++d) {
            std::vector<bool> usedBuilding(inst.buildings.size(), false);

            // Scan all placements and mark buildings used by this group on this day.
            for (const Placement& p : placements) {
                if (p.activityId < 0) continue;
                if (p.day != d) continue;
                const Activity& act = inst.activities[p.activityId];

                bool attends = false;
                for (int gid : act.groupIds) {
                    if (gid == groupId) {
                        attends = true;
                        break;
                    }
                }
                if (!attends) continue;

                int bIdx = roomIndexToBuildingIndex(p.roomIndex);
                if (bIdx >= 0 && bIdx < (int)usedBuilding.size()) {
                    usedBuilding[bIdx] = true;
                }
            }

            int countBuildings = 0;
            for (bool used : usedBuilding) {
                if (used) countBuildings++;
            }
            // Allow up to 2 buildings; penalize each extra building.
            if (countBuildings > 2) {
                score += (countBuildings - 2);
            }
        }
    }

    // Professor-level building diversity per day.
    for (int pr = 0; pr < numProfs; ++pr) {
        int profId = inst.professors[pr].id;
        for (int d = 0; d < DAYS; ++d) {
            std::vector<bool> usedBuilding(inst.buildings.size(), false);

            // Mark buildings used by this professor on this day.
            for (const Placement& p : placements) {
                if (p.activityId < 0) continue;
                if (p.day != d) continue;
                const Activity& act = inst.activities[p.activityId];
                if (act.profId != profId) continue;

                int bIdx = roomIndexToBuildingIndex(p.roomIndex);
                if (bIdx >= 0 && bIdx < (int)usedBuilding.size()) {
                    usedBuilding[bIdx] = true;
                }
            }

            int countBuildings = 0;
            for (bool used : usedBuilding) {
                if (used) countBuildings++;
            }
            if (countBuildings > 2) {
                score += (countBuildings - 2);
            }
        }
    }

    return score;
}
