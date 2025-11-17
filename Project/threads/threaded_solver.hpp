#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include "solver_base.hpp"
#include <optional>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Multithreaded backtracking solver for timetable generation.
 *
 * Builds a frontier of partial states up to a given depth, then explores the
 * remaining search space in parallel using multiple worker threads.
 */
class ThreadedBacktrackingSolver {
public:
    /**
     * @brief Create a threaded backtracking solver.
     *
     * @param maxSolutions Maximum number of complete solutions to process before stopping.
     * @param numThreads   Number of worker threads used in the parallel phase.
     * @param frontierDepth Depth at which the initial sequential search stops
     *                      and frontier states are generated.
     */
    ThreadedBacktrackingSolver(int maxSolutions, int numThreads, int frontierDepth);

    /**
     * @brief Solve the given timetable problem.
     *
     * @param inst Problem instance with activities, resources and constraints.
     * @return Best solution found, or std::nullopt if no feasible timetable exists.
     */
    std::optional<TimetableSolution> solve(const ProblemInstance& inst);

private:
    /**
     * @brief Partially constructed timetable at a given search depth.
     *
     * Used as a frontier node from which worker threads continue backtracking.
     */
    struct PartialState {
        TimetableState state; ///< Incremental timetable state.
        std::vector<Placement> placements; ///< Placements chosen so far.
        int depth; ///< Index in the ordered activity list.
    };

    /// Pointer to the problem instance being solved (valid only during solve()).
    const ProblemInstance* inst_ = nullptr;

    int maxSolutions_; ///< Max number of solutions to process before termination.
    int numThreads_; ///< Number of worker threads.
    int frontierDepth_; ///< Depth where sequential search hands off to threads.

    // Shared across workers
    TimetableSolution best_; ///< Best solution found so far.
    int bestScore_ = std::numeric_limits<int>::max(); ///< Score of best_ (lower is better).
    int solutionsFound_ = 0; ///< Number of complete solutions found.

    std::mutex bestMutex_; ///< Guards best_, bestScore_ and solutionsFound_.
    std::mutex queueMutex_; ///< Guards access to frontierIndex_.

    std::vector<Activity> orderedActivities_; ///< Activities ordered for backtracking.

    // Frontier of partial states at depth frontierDepth_
    std::vector<std::unique_ptr<PartialState>> frontier_; ///< Shared pool of partial states.
    size_t frontierIndex_ = 0; ///< Next frontier index to claim.

    /**
     * @brief Compute a heuristic ordering of activities.
     *
     * Typically orders more constrained or harder activities first.
     */
    void orderActivities(const ProblemInstance& inst);

    /**
     * @brief Build the frontier_ vector using a sequential DFS up to frontierDepth_.
     */
    void buildFrontierSequential();

    /**
     * @brief Recursive DFS helper used while building the frontier.
     *
     * @param state      Current incremental timetable state.
     * @param placements Placements on the current path.
     * @param depth      Current depth in orderedActivities_.
     */
    void buildFrontierDFS(TimetableState& state,  std::vector<Placement>& placements, int depth);

    /**
     * @brief Main loop executed by each worker thread.
     *
     * Continuously claims partial states from frontier_ and explores them.
     */
    void workerLoop();

    /**
     * @brief Continue backtracking from a given partial state.
     *
     * @param partial Starting partial assignment for this search branch.
     */
    void backtrackFromPartial(const PartialState& partial);

    /**
     * @brief Compute the objective score of a complete timetable.
     *
     * @param placements All placements describing a full timetable.
     * @return Objective score (lower is better).
     */
    int computeScore(const std::vector<Placement>& placements) const;
};
