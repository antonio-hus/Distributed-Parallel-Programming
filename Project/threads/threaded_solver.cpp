///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "threaded_solver.hpp"
#include <algorithm>
#include <future>


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
          frontierDepth_(frontierDepth) // Unused in dynamic splitting
{}

/**
 * @brief Entry point for solving a timetable instance.
 *
 * Resets internal state, then launches parallel recursive DFS with dynamic thread splitting
 * at every branching point. Returns the best solution found or std::nullopt if none.
 */
std::optional<TimetableSolution> ThreadedBacktrackingSolver::solve(const ProblemInstance& inst) {
    inst_ = &inst;
    orderActivities(inst);

    // Reset shared state before starting a new search.
    best_.placements.clear();
    bestScore_ = std::numeric_limits<int>::max();
    solutionsFound_ = 0;
    found_ = false;

    TimetableState state(*inst_); // initial state
    std::vector<Placement> placements(inst_->activities.size());
    for (auto& p : placements) {
        p.activityId = -1;
        p.day = 0; p.slot = 0; p.roomIndex = 0;
    }

    // Launch recursive parallel search.
    parallelDFS(state, placements, 0, numThreads_);

    if (solutionsFound_ == 0) {
        return std::nullopt;
    }
    return best_;
}

/**
 * @brief Compute a heuristic ordering of activities for backtracking.
 *
 * Prioritizes course activities, then sorts by number of student groups descending.
 * More constrained activities are assigned earlier in the search.
 */
void ThreadedBacktrackingSolver::orderActivities(const ProblemInstance& inst) {
    orderedActivities_ = inst.activities;

    std::sort(orderedActivities_.begin(), orderedActivities_.end(),
              [](const Activity& a, const Activity& b) {
                  // Put COURSES before other types.
                  if (a.type != b.type) return a.type == ActivityType::COURSE;
                  // Within type, schedule activities with more groups first.
                  return a.groupIds.size() > b.groupIds.size();
              });
}

/**
 * @brief Main recursive DFS with dynamic thread splitting.
 *
 * At each assignment, splits available worker threads across all feasible placements,
 * spawning parallel tasks for each branch. Falls back to sequential DFS if only one thread left.
 * Terminates early if enough solutions found globally.
 */
void ThreadedBacktrackingSolver::parallelDFS(
        TimetableState& state, std::vector<Placement>& placements, int depth, int threadsLeft) {

    if (found_) return;

    if (depth == (int)orderedActivities_.size()) {
        // Complete assignment! Validate and update result.
        if (!state.checkFinalWorkloadBounds()) return;
        int score = computeScore(placements);

        std::lock_guard<std::mutex> lock(bestMutex_);
        if (score < bestScore_) {
            bestScore_ = score;
            best_.placements = placements;
            best_.score = score;
        }
        ++solutionsFound_;
        if (solutionsFound_ >= maxSolutions_) found_ = true;
        return;
    }

    const Activity& act = orderedActivities_[depth];
    int numRooms = (int)inst_->rooms.size();

    // Gather all feasible placements for this activity.
    struct NextPlacement { int day, slot, roomIdx; };
    std::vector<NextPlacement> nexts;

    for (int day = 0; day < DAYS; ++day) {
        for (int slot = 0; slot < SLOTS_PER_DAY; ++slot) {
            for (int roomIdx = 0; roomIdx < numRooms; ++roomIdx) {
                const Room& room = inst_->rooms[roomIdx];
                // Enforce room/activity type compatibility.
                if (act.type == ActivityType::COURSE && room.type != Room::Type::COURSE) continue;
                if (act.type == ActivityType::SEMINAR && room.type != Room::Type::SEMINAR) continue;
                if (act.type == ActivityType::LAB && room.type != Room::Type::LAB) continue;
                if (state.place(act, day, slot, roomIdx)) {
                    nexts.push_back({day, slot, roomIdx});
                    state.undo(act, day, slot, roomIdx);
                }
            }
        }
    }
    int choices = (int)nexts.size();
    if (choices == 0) return;
    if (found_) return;

    if (threadsLeft <= 1 || choices == 1) {
        // No parallelism left; explore sequentially.
        for (const auto& np : nexts) {
            if (found_) break;
            if (!state.place(act, np.day, np.slot, np.roomIdx)) continue;
            Placement p{act.id, np.day, np.slot, np.roomIdx};
            placements[act.id] = p;
            parallelDFS(state, placements, depth + 1, 1);
            state.undo(act, np.day, np.slot, np.roomIdx);
        }
    } else {
        // Parallel split: allocate threads to branches, launch async tasks.
        std::vector<std::future<void>> tasks;
        int base = threadsLeft / choices, extra = threadsLeft % choices;
        for (int i = 0; i < choices; ++i) {
            if (found_) break;
            int threadsForBranch = base + (i < extra ? 1 : 0);
            TimetableState nextState = state;
            std::vector<Placement> nextPlacements = placements;
            const auto& np = nexts[i];
            if (!nextState.place(act, np.day, np.slot, np.roomIdx)) continue;
            Placement p{act.id, np.day, np.slot, np.roomIdx};
            nextPlacements[act.id] = p;
            tasks.push_back(std::async(std::launch::async,
                                       [this, nextState, nextPlacements, depth, threadsForBranch]() mutable {
                                           this->parallelDFS(nextState, nextPlacements, depth + 1, threadsForBranch);
                                       }));
        }
        for (auto& t : tasks) t.wait();
    }
}

/**
 * @brief Compute soft-constraint score for a complete timetable.
 *
 * Lower scores are better. Penalties include:
 *  - Late slot penalties (activities scheduled late in day)
 *  - Gap penalties (idle slots for groups and professors)
 *  - Building locality penalties (using >2 buildings/day)
 */
int ThreadedBacktrackingSolver::computeScore(const std::vector<Placement>& placements) const {
    const ProblemInstance& inst = *inst_;
    int score = 0;
    for (const Placement& p : placements) {
        if (p.activityId < 0) continue;
        if (p.slot >= 4) score += 1;
    }
    int numGroups = (int)inst.groups.size();
    std::vector<std::vector<std::vector<bool>>> groupDaySlots(
            numGroups,
            std::vector<std::vector<bool>>(DAYS, std::vector<bool>(SLOTS_PER_DAY, false))
    );
    for (const Placement& p : placements) {
        if (p.activityId < 0) continue;
        const Activity& act = inst.activities[p.activityId];
        for (int gid : act.groupIds) {
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
    for (int g = 0; g < numGroups; ++g) {
        for (int d = 0; d < DAYS; ++d) {
            int first = -1, last = -1;
            for (int s = 0; s < SLOTS_PER_DAY; ++s) {
                if (groupDaySlots[g][d][s]) {
                    if (first == -1) first = s;
                    last = s;
                }
            }
            if (first == -1 || last == -1 || first == last) continue;
            int gaps = 0;
            for (int s = first; s <= last; ++s) {
                if (!groupDaySlots[g][d][s]) gaps++;
            }
            score += gaps;
        }
    }
    int numProfs = (int)inst.professors.size();
    std::vector<std::vector<std::vector<bool>>> profDaySlots(
            numProfs,
            std::vector<std::vector<bool>>(DAYS, std::vector<bool>(SLOTS_PER_DAY, false))
    );
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
    for (int pr = 0; pr < numProfs; ++pr) {
        for (int d = 0; d < DAYS; ++d) {
            int first = -1, last = -1;
            for (int s = 0; s < SLOTS_PER_DAY; ++s) {
                if (profDaySlots[pr][d][s]) {
                    if (first == -1) first = s;
                    last = s;
                }
            }
            if (first == -1 || last == -1 || first == last) continue;
            int gaps = 0;
            for (int s = first; s <= last; ++s) {
                if (!profDaySlots[pr][d][s]) gaps++;
            }
            score += gaps;
        }
    }
    auto roomIndexToBuildingIndex = [&](int roomIndex) -> int {
        if (roomIndex < 0 || roomIndex >= (int)inst.rooms.size()) return -1;
        return inst.rooms[roomIndex].buildingId;
    };
    for (int g = 0; g < numGroups; ++g) {
        int groupId = inst.groups[g].id;
        for (int d = 0; d < DAYS; ++d) {
            std::vector<bool> usedBuilding(inst.buildings.size(), false);
            for (const Placement& p : placements) {
                if (p.activityId < 0) continue;
                if (p.day != d) continue;
                const Activity& act = inst.activities[p.activityId];
                bool attends = false;
                for (int gid : act.groupIds) {
                    if (gid == groupId) { attends = true; break; }
                }
                if (!attends) continue;
                int bIdx = roomIndexToBuildingIndex(p.roomIndex);
                if (bIdx >= 0 && bIdx < (int)usedBuilding.size()) {
                    usedBuilding[bIdx] = true;
                }
            }
            int countBuildings = 0;
            for (bool used : usedBuilding) if (used) countBuildings++;
            if (countBuildings > 2) score += (countBuildings - 2);
        }
    }
    for (int pr = 0; pr < numProfs; ++pr) {
        int profId = inst.professors[pr].id;
        for (int d = 0; d < DAYS; ++d) {
            std::vector<bool> usedBuilding(inst.buildings.size(), false);
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
            for (bool used : usedBuilding) if (used) countBuildings++;
            if (countBuildings > 2) score += (countBuildings - 2);
        }
    }
    return score;
}
