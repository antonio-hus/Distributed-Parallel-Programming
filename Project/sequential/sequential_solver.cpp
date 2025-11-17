///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "sequential_solver.hpp"
#include <algorithm>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Construct a sequential backtracking solver with a solution limit.
 *
 * @param maxSolutions Maximum number of complete solutions to process
 *                     before stopping (1 = stop after first solution).
 */
SequentialBacktrackingSolver::SequentialBacktrackingSolver(int maxSolutions)
        : maxSolutions_(maxSolutions) {}

/**
 * @brief Solve a timetable instance using depth-first backtracking.
 *
 * Sets up the working TimetableState, orders activities, initializes the
 * placement array, then recursively explores all valid assignments until
 * a solution limit is reached. Returns the best solution found or nullopt.
 */
std::optional<TimetableSolution> SequentialBacktrackingSolver::solve(const ProblemInstance& inst) {
    inst_ = &inst;

    // Local mutable state used during the search.
    TimetableState state(inst);
    state_ = &state;

    // Start from the raw activity list, then reorder by heuristic.
    orderedActivities_ = inst.activities;
    orderActivities();

    // Reset best solution tracking.
    best_.placements.clear();
    bestScore_ = std::numeric_limits<int>::max();
    solutionsFound_ = 0;

    // Allocate one placement entry per activity id and mark as unused.
    std::vector<Placement> currentPlacements(inst.activities.size());
    for (auto& p : currentPlacements) {
        p.activityId = -1;
        p.day        = 0;
        p.slot       = 0;
        p.roomIndex  = 0;
    }

    // Launch recursive depth-first search from the first activity.
    backtrack(0, currentPlacements);

    // If no complete solution was found, signal failure.
    if (solutionsFound_ == 0) {
        return std::nullopt;
    }
    return best_;
}

/**
 * @brief Order activities to improve backtracking efficiency.
 *
 * Current heuristic: place COURSE activities first, then break ties by
 * scheduling activities with more groups earlier.
 */
void SequentialBacktrackingSolver::orderActivities() {
    std::sort(orderedActivities_.begin(), orderedActivities_.end(),
              [](const Activity& a, const Activity& b) {
                  if (a.type != b.type) {
                      // Courses first, then other types.
                      return a.type == ActivityType::COURSE;
                  }
                  // For the same type, activities involving more groups first.
                  return a.groupIds.size() > b.groupIds.size();
              });
}

/**
 * @brief Recursive depth-first search over activity placements.
 *
 * At each depth, chooses the next activity from orderedActivities_ and
 * tries all feasible (day, slot, room) placements. When all activities
 * are placed and final workloads are valid, computes and updates the
 * best solution.
 */
void SequentialBacktrackingSolver::backtrack(int depth, std::vector<Placement>& currentPlacements) {
    // Stop early if solution limit has been reached.
    if (solutionsFound_ >= maxSolutions_) return;

    // All activities assigned: check final constraints and evaluate solution.
    if (depth == (int)orderedActivities_.size()) {
        // Some workload bounds require a full timetable to check.
        if (!state_->checkFinalWorkloadBounds()) {
            return;
        }

        int score = computeScore(currentPlacements);
        if (score < bestScore_) {
            bestScore_ = score;
            best_.placements = currentPlacements;
            best_.score = score;
        }
        solutionsFound_++;
        return;
    }

    const Activity& act = orderedActivities_[depth];

    int numRooms = (int)inst_->rooms.size();
    // Try each (day, slot, room) as a candidate placement for this activity.
    for (int day = 0; day < DAYS; ++day) {
        for (int slot = 0; slot < SLOTS_PER_DAY; ++slot) {
            for (int roomIdx = 0; roomIdx < numRooms; ++roomIdx) {
                const Room& room = inst_->rooms[roomIdx];

                // Quick filter: enforce room type compatibility with activity type.
                if (act.type == ActivityType::COURSE && room.type != Room::Type::COURSE)
                    continue;
                if (act.type == ActivityType::SEMINAR && room.type != Room::Type::SEMINAR)
                    continue;
                if (act.type == ActivityType::LAB && room.type != Room::Type::LAB)
                    continue;

                // Check all hard constraints and tentatively commit if valid.
                if (state_->place(act, day, slot, roomIdx)) {
                    Placement p{act.id, day, slot, roomIdx};
                    // Store placement by activity id (assumed to be 0..N-1).
                    currentPlacements[act.id] = p;
                    // Recurse to place the next activity.
                    backtrack(depth + 1, currentPlacements);
                    // Backtrack: remove the placement from the state.
                    state_->undo(act, day, slot, roomIdx);
                }
            }
        }
    }
}

/**
 * @brief Compute soft-constraint score for a complete timetable.
 *
 * The score aggregates:
 *  - penalties for late time slots,
 *  - gap penalties for student groups and professors,
 *  - building locality penalties for groups and professors using
 *    more than two buildings in a day.
 */
int SequentialBacktrackingSolver::computeScore(const std::vector<Placement>& placements) const {
    const ProblemInstance& inst = *inst_;

    int score = 0;

    // LATE SLOT PENALTY
    // Penalize activities placed in late time slots (e.g., 16:00–20:00).
    for (const Placement& p : placements) {
        if (p.activityId < 0) continue;
        if (p.slot >= 4) {
            // slots 4 and 5 => late in the day.
            score += 1;
        }
    }

    // GROUP GAP PENALTY
    int numGroups = (int)inst.groups.size();

    // groupDaySlots[group][day][slot] = whether the group has a class then.
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

    // For each group and day, penalize idle slots between first and last activity.
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
    // For each (group, day) and (professor, day), count distinct buildings used.
    // If more than 2 buildings are used, penalize the extra ones.

    auto roomIndexToBuildingIndex = [&](int roomIndex) -> int {
        if (roomIndex < 0 || roomIndex >= (int)inst.rooms.size()) return -1;
        // In this demo, buildingId is assumed to be a valid index.
        return inst.rooms[roomIndex].buildingId;
    };

    // Group-level building diversity per day.
    for (int g = 0; g < numGroups; ++g) {
        int groupId = inst.groups[g].id;
        for (int d = 0; d < DAYS; ++d) {
            std::vector<bool> usedBuilding(inst.buildings.size(), false);

            // Scan all placements for this group on this day.
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
            for (bool used : usedBuilding) {
                if (used) countBuildings++;
            }
            if (countBuildings > 2) {
                // Each building beyond the second adds one penalty point.
                score += (countBuildings - 2);
            }
        }
    }

    // Professor-level building diversity per day.
    for (int pr = 0; pr < numProfs; ++pr) {
        int profId = inst.professors[pr].id;
        for (int d = 0; d < DAYS; ++d) {
            std::vector<bool> usedBuilding(inst.buildings.size(), false);

            // Mark buildings where this professor teaches on this day.
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
