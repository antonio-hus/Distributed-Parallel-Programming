#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include "solver_base.hpp"
#include <optional>
#include <vector>
#include <mutex>
#include <atomic>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Multithreaded backtracking solver for timetable generation.
 *
 * Uses dynamic thread-splitting at each branch in the search tree, where all feasible choices
 * for the current activity are divided among the available worker threads. Each thread explores
 * its assigned subtree in parallel, with early termination and thread-safe best solution selection.
 */
class ThreadedBacktrackingSolver {
public:
    /**
     * @brief Create a threaded backtracking solver.
     *
     * @param maxSolutions   Maximum number of complete solutions to process before stopping.
     * @param numThreads     Number of worker threads for parallel exploration.
     * @param frontierDepth  (Unused in dynamic mode, kept for API compatibility)
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
    /// Pointer to the problem instance being solved (valid only during solve()).
    const ProblemInstance* inst_ = nullptr;

    int maxSolutions_;   ///< Max number of solutions to process before termination.
    int numThreads_;     ///< Number of worker threads.
    int frontierDepth_;  ///< Unused in dynamic mode.

    // Shared state across workers
    TimetableSolution best_;   ///< Best solution found so far.
    int bestScore_ = std::numeric_limits<int>::max(); ///< Score of best_ (lower is better).
    int solutionsFound_ = 0;   ///< Number of complete solutions found.

    std::mutex bestMutex_;     ///< Guards best_, bestScore_ and solutionsFound_.
    std::atomic<bool> found_{false}; ///< Signals early termination to all threads.

    std::vector<Activity> orderedActivities_; ///< Activities ordered for backtracking.

    /**
     * @brief Compute a heuristic ordering of activities (hardest first).
     */
    void orderActivities(const ProblemInstance& inst);

    /**
     * @brief Main recursive DFS with dynamic thread splitting.
     *
     * At each activity assignment point, splits available worker threads across all feasible placements,
     * spawning parallel tasks with balanced thread allocation, and sequential fallback when threads run out.
     */
    void parallelDFS(TimetableState& state, std::vector<Placement>& placements, int depth, int threadsLeft);

    /**
     * @brief Compute the objective score of a complete timetable.
     *
     * @param placements All placements describing a full timetable.
     * @return Objective score (lower is better).
     */
    int computeScore(const std::vector<Placement>& placements) const;
};
