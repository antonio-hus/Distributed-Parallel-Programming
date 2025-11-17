///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "opencl_solver.hpp"
#include <limits>
#include <iostream>
#include <algorithm>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Construct the OpenCL exhaustive solver with limits and defaults.
 *
 * Initializes the maximum number of solutions, batch size, and sets the
 * best score to a very large value so any real score will improve it.
 */
OpenCLExhaustiveSolver::OpenCLExhaustiveSolver(int maxSolutions, int batchSize)
        : maxSolutions_(maxSolutions),
          batchSize_(batchSize) {
    best_.score = std::numeric_limits<int>::max();
}

/**
 * @brief Send the accumulated batch of complete timetables to the GPU.
 *
 * Uses the OpenCL context to evaluate validity and score for each candidate
 * timetable in batch_. Updates the best solution if a lower score is found,
 * then clears the batch for reuse.
 */
void OpenCLExhaustiveSolver::flushBatchToGPU(const ProblemInstance& inst) {
    if (batch_.empty()) return;

    std::vector<int> validFlags;
    std::vector<int> scores;

    // Evaluate all timetables in the current batch on the device.
    clctx_.evaluateBatch(inst, batch_, validFlags, scores);

    // Scan results and update global best solution if needed.
    for (int i = 0; i < (int)batch_.size(); ++i) {
        if (!validFlags[i]) continue;
        if (scores[i] < best_.score) {
            best_.score = scores[i];
            best_.placements = batch_[i];
        }
    }

    batch_.clear();
}

/**
 * @brief Recursive CPU-side DFS that enumerates all feasible timetables.
 *
 * Builds timetables one activity at a time using TimetableState to enforce
 * hard constraints. Whenever a full assignment is produced, it is appended
 * to batch_ for later GPU evaluation. Stops when maxSolutions_ have been
 * generated.
 */
void OpenCLExhaustiveSolver::dfs(
        const ProblemInstance& inst,
        TimetableState& state,
        std::vector<Placement>& placements,
        const std::vector<Activity>& ordered,
        int depth) {

    // Early stop if enough solutions have already been generated.
    if ((int)solutionsFound_.load(std::memory_order_relaxed) >= maxSolutions_) {
        return;
    }

    // All activities have been assigned: enqueue this complete timetable.
    if (depth == (int)ordered.size()) {
        batch_.push_back(placements);
        solutionsFound_.fetch_add(1, std::memory_order_relaxed);

        // When we reach batchSize_, push work to the GPU.
        if ((int)batch_.size() >= batchSize_) {
            flushBatchToGPU(inst);
        }
        return;
    }

    const Activity& act = ordered[depth];
    int numRooms = (int)inst.rooms.size();

    // Try every room/time combination for the current activity.
    for (int day = 0; day < DAYS; ++day) {
        for (int slot = 0; slot < SLOTS_PER_DAY; ++slot) {
            for (int roomIdx = 0; roomIdx < numRooms; ++roomIdx) {
                const Room& room = inst.rooms[roomIdx];

                // Fast filter: enforce room/activity type compatibility.
                if (act.type == ActivityType::COURSE && room.type != Room::Type::COURSE)
                    continue;
                if (act.type == ActivityType::SEMINAR && room.type != Room::Type::SEMINAR)
                    continue;
                if (act.type == ActivityType::LAB && room.type != Room::Type::LAB)
                    continue;

                // Check all remaining hard constraints via TimetableState.
                if (state.place(act, day, slot, roomIdx)) {
                    placements[act.id] = {act.id, day, slot, roomIdx};
                    dfs(inst, state, placements, ordered, depth + 1);
                    state.undo(act, day, slot, roomIdx);
                    placements[act.id].activityId = -1;

                    // Re-check stop condition after returning from deeper levels.
                    if ((int)solutionsFound_.load(std::memory_order_relaxed) >= maxSolutions_) {
                        return;
                    }
                }
            }
        }
    }
}

/**
 * @brief Run the exhaustive CPU DFS + GPU scoring pipeline on a problem.
 *
 * Initializes state and placements, orders activities by a simple heuristic,
 * runs DFS to generate complete timetables, flushes any remaining batch to
 * the GPU, and finally returns the best solution found (or std::nullopt).
 */
std::optional<TimetableSolution> OpenCLExhaustiveSolver::solve(const ProblemInstance& inst) {
    TimetableState state(inst);

    // One placement per activity id; initialize as unused.
    std::vector<Placement> placements(inst.activities.size());
    for (auto& p : placements) {
        p.activityId = -1;
        p.day = 0;
        p.slot = 0;
        p.roomIndex = 0;
    }

    // Heuristic ordering: courses first, then more-group activities.
    std::vector<Activity> ordered = inst.activities;
    std::sort(ordered.begin(), ordered.end(),
              [](const Activity& a, const Activity& b) {
                  if (a.type != b.type) {
                      return a.type == ActivityType::COURSE;
                  }
                  return a.groupIds.size() > b.groupIds.size();
              });

    std::cout << "OpenCLExhaustiveSolver: starting exhaustive DFS, batchSize="
              << batchSize_ << ", maxSolutions=" << maxSolutions_ << "\n";

    // Reset solver state for this run.
    solutionsFound_.store(0, std::memory_order_relaxed);
    batch_.clear();
    best_.score = std::numeric_limits<int>::max();
    best_.placements.clear();

    // Enumerate all feasible timetables (up to maxSolutions_).
    dfs(inst, state, placements, ordered, 0);

    // Evaluate any remaining timetables that did not trigger a flush.
    flushBatchToGPU(inst);

    // If best score is unchanged, no valid schedule was found.
    if (best_.score == std::numeric_limits<int>::max()) {
        std::cout << "OpenCLExhaustiveSolver: no valid timetable found.\n";
        return std::nullopt;
    }

    std::cout << "OpenCLExhaustiveSolver: best score = " << best_.score
              << " (solutions generated: " << solutionsFound_.load() << ")\n";
    return best_;
}
