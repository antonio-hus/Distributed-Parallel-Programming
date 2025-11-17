#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include "solver_base.hpp"
#include "../threads/threaded_solver.hpp"
#include <optional>
#include <vector>


///////////////////////////
///       SOLVER        ///
///////////////////////////
/**
 * @brief MPI-based multi-start wrapper around the threaded backtracking solver.
 *
 * Each MPI rank builds and solves its own randomized variant of the same
 * problem (multi-start), using an internal ThreadedBacktrackingSolver for
 * intra-node parallelism. Rank 0 gathers candidate solutions from all ranks
 * and returns the best one, while non-root ranks return std::nullopt.
 */
class MPIHybridMultiStartSolver {
public:
    /**
     * @brief Construct a hybrid MPI + threaded solver.
     *
     * @param maxSolutions Maximum number of complete solutions each rank's
     *                     threaded solver is allowed to process before stopping.
     * @param numThreads   Number of worker threads used inside each rank.
     */
    MPIHybridMultiStartSolver(int maxSolutions, int numThreads);

    /**
     * @brief Solve the problem cooperatively across all MPI ranks.
     *
     * Must be called on every MPI rank. Each rank runs a local threaded search,
     * then rank 0 collects and compares solutions from all ranks and returns
     * the globally best timetable. Other ranks return std::nullopt.
     *
     * @param inst Problem instance to solve (broadcast or replicated on all ranks).
     * @return std::optional<TimetableSolution> Best solution on rank 0,
     *         std::nullopt on other ranks or if no solution exists.
     */
    std::optional<TimetableSolution> solve(const ProblemInstance& inst);

private:
    /// Per-rank limit on how many solutions the threaded solver explores.
    int maxSolutions_;

    /// Number of worker threads used within each MPI process.
    int numThreads_;

    /**
     * @brief Serialize a placement vector into a flat integer buffer.
     *
     * Encodes (activityId, day, slot, roomIndex) for each placement in order,
     * so it can be sent via MPI as a contiguous array of ints.
     *
     * @param placements Input placement list to serialize.
     * @param buffer     Output flat buffer; resized and filled by this function.
     */
    static void serializePlacements(const std::vector<Placement>& placements, std::vector<int>& buffer);

    /**
     * @brief Deserialize a flat integer buffer into a placement vector.
     *
     * Reconstructs Placement objects from a flat buffer created by
     * serializePlacements(), restoring the original placement list.
     *
     * @param buffer      Flat integer buffer received via MPI.
     * @param placements  Output placement list; resized and filled by this function.
     */
    static void deserializePlacements(const std::vector<int>& buffer, std::vector<Placement>& placements);
};
