#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include <optional>

///////////////////////////
///        TYPES        ///
///////////////////////////
/**
 * @brief Full timetable solution and its associated score.
 *
 * Holds all activity placements making up a complete timetable together
 * with the value of the soft-constraint objective function (lower is better).
 */
struct TimetableSolution {
    /// Placements indexed by activity id (or equivalent stable activity index).
    std::vector<Placement> placements;

    /// Soft-constraint score for this timetable; lower values are preferred.
    int score;
};


///////////////////////////
///      INTERFACE      ///
///////////////////////////
/**
 * @brief Common interface for timetable solvers.
 *
 * Implementations may be sequential, multithreaded, GPU-accelerated,
 * or distributed via MPI, but all expose the same solve() contract.
 */
class ISolver {
public:
    virtual ~ISolver() = default;

    /**
     * @brief Solve the given problem instance and return a timetable.
     *
     * Implementations should either return a feasible TimetableSolution
     * (with all hard constraints satisfied) or std::nullopt if no such
     * timetable can be found under their search strategy/limits.
     */
    virtual std::optional<TimetableSolution> solve(const ProblemInstance& inst) = 0;
};
