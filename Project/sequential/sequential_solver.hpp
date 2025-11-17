#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include "solver_base.hpp"
#include <optional>
#include <limits>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Single-threaded backtracking solver for timetable generation.
 *
 * Explores the search space with a depth-first search, maintaining a single
 * TimetableState and tracking the best solution found according to a scoring
 * function over soft constraints.
 */
class SequentialBacktrackingSolver {
public:
    /**
     * @brief Construct a sequential backtracking solver.
     *
     * @param maxSolutions Maximum number of complete solutions to process
     *                     before stopping the search (1 by default).
     */
    explicit SequentialBacktrackingSolver(int maxSolutions = 1);

    /**
     * @brief Solve the given timetable problem instance.
     *
     * Initializes internal state, orders activities, runs recursive
     * backtracking and returns the best solution found, or std::nullopt
     * if no valid timetable exists.
     */
    std::optional<TimetableSolution> solve(const ProblemInstance& inst);

private:
    /// Problem instance being solved (owned externally, valid only during solve()).
    const ProblemInstance* inst_ = nullptr;

    /// Pointer to the mutable timetable state used during backtracking.
    TimetableState* state_ = nullptr;

    /// Activities reordered by a heuristic to reduce branching.
    std::vector<Activity> orderedActivities_;

    /// Best solution found so far.
    TimetableSolution best_;

    /// Score of the best solution (lower is better).
    int bestScore_ = std::numeric_limits<int>::max();

    /// Maximum number of solutions to accept before stopping.
    int maxSolutions_;

    /// Number of complete solutions explored so far.
    int solutionsFound_ = 0;

    /**
     * @brief Compute an ordering of activities for backtracking.
     *
     * Typically sorts activities to place more constrained or important
     * ones earlier in the search.
     */
    void orderActivities();

    /**
     * @brief Recursive depth-first search over activity assignments.
     *
     * @param depth Current index in orderedActivities_.
     * @param currentPlacements Vector of placements indexed by activity id,
     *                          representing the current partial timetable.
     */
    void backtrack(int depth, std::vector<Placement>& currentPlacements);

    /**
     * @brief Compute the score of a complete timetable.
     *
     * Uses the placement list to evaluate soft constraints and return an
     * objective value; lower scores represent better timetables.
     */
    int computeScore(const std::vector<Placement>& placements) const;
};
