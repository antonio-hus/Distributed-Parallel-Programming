#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include "solver_base.hpp"
#include "opencl_evaluator.hpp"
#include <optional>
#include <vector>
#include <atomic>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Exhaustive timetable solver that offloads scoring to OpenCL.
 *
 * Enumerates complete timetables with a depth-first search on the CPU,
 * collects them in batches, and uses a GPU/OpenCL kernel to evaluate
 * soft-constraint scores for all solutions in a batch at once.
 */
class OpenCLExhaustiveSolver {
public:
    /**
     * @brief Construct an OpenCL-based exhaustive solver.
     *
     * @param maxSolutions Maximum number of complete solutions to evaluate
     *                     before stopping the search.
     * @param batchSize    Number of candidate timetables to accumulate in a
     *                     batch before sending them to the GPU for scoring.
     */
    OpenCLExhaustiveSolver(int maxSolutions, int batchSize);

    /**
     * @brief Solve the given timetable instance using CPU search + GPU scoring.
     *
     * Runs a depth-first enumeration of all feasible timetables, periodically
     * sending batches of complete schedules to the OpenCL evaluator. Returns
     * the best timetable found, or std::nullopt if no valid timetable exists.
     */
    std::optional<TimetableSolution> solve(const ProblemInstance& inst);

private:
    /// Maximum number of solutions to process before terminating the search.
    int maxSolutions_;

    /// Target number of complete timetables per GPU scoring batch.
    int batchSize_;

    /// OpenCL context and kernels used for batched timetable evaluation.
    TimetableOpenCLContext clctx_;

    /// Accumulated batch of complete placement vectors awaiting GPU scoring.
    std::vector<std::vector<Placement>> batch_;

    /// Best solution found so far (placements + score).
    TimetableSolution best_;

    /// Number of complete solutions discovered so far (updated across DFS calls).
    std::atomic<int> solutionsFound_{0};

    /**
     * @brief Recursive DFS that builds complete timetables.
     *
     * Extends the current partial placement in-place, respecting hard constraints
     * via TimetableState. Whenever a full assignment is produced, it is pushed
     * into batch_ for later evaluation on the GPU.
     *
     * @param inst       Problem instance being solved.
     * @param state      Mutable timetable state tracking resource usage and constraints.
     * @param placements Current placements indexed by activity id.
     * @param ordered    Activity ordering to follow during the search.
     * @param depth      Current index in the ordered activity list.
     */
    void dfs(const ProblemInstance& inst,
             TimetableState& state,
             std::vector<Placement>& placements,
             const std::vector<Activity>& ordered,
             int depth);

    /**
     * @brief Send the current batch of complete timetables to the GPU.
     *
     * Uses the OpenCL context to compute scores for all timetables in batch_,
     * updates the global best_ if any schedule is better, and clears the batch
     * for reuse.
     *
     * @param inst Problem instance used to interpret placements on the device.
     */
    void flushBatchToGPU(const ProblemInstance& inst);
};
