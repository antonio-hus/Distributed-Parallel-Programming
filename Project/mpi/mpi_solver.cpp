///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "mpi_solver.hpp"
#include "formatting.hpp"
#include <mpi.h>
#include <algorithm>
#include <random>
#include <limits>
#include <iostream>


///////////////////////////
///       SOLVERS       ///
///////////////////////////
/**
 * @brief Construct the hybrid MPI + threaded multi-start solver.
 *
 * @param maxSolutions Maximum number of complete solutions each rank's
 *                     threaded solver is allowed to explore.
 * @param numThreads   Number of worker threads used on each MPI rank.
 */
MPIHybridMultiStartSolver::MPIHybridMultiStartSolver(int maxSolutions, int numThreads)
        : maxSolutions_(maxSolutions),
          numThreads_(numThreads) {}

/**
 * @brief Serialize a placement vector into a flat integer buffer.
 *
 * Encodes each placement as four consecutive integers:
 * (activityId, day, slot, roomIndex), suitable for MPI send/recv.
 */
void MPIHybridMultiStartSolver::serializePlacements(const std::vector<Placement>& placements, std::vector<int>& buffer) {
    buffer.clear();
    buffer.reserve(4 * placements.size());
    for (const Placement& p : placements) {
        buffer.push_back(p.activityId);
        buffer.push_back(p.day);
        buffer.push_back(p.slot);
        buffer.push_back(p.roomIndex);
    }
}

/**
 * @brief Deserialize a flat integer buffer into a placement vector.
 *
 * Reconstructs the original placements from a buffer produced by
 * serializePlacements(), assuming groups of four ints per placement.
 */
void MPIHybridMultiStartSolver::deserializePlacements(const std::vector<int>& buffer, std::vector<Placement>& placements) {
    size_t count = buffer.size() / 4;
    placements.resize(count);
    for (size_t i = 0; i < count; ++i) {
        placements[i].activityId = buffer[4 * i + 0];
        placements[i].day        = buffer[4 * i + 1];
        placements[i].slot       = buffer[4 * i + 2];
        placements[i].roomIndex  = buffer[4 * i + 3];
    }
}

/**
 * @brief Solve the instance using multi-start across MPI ranks plus threads per rank.
 *
 * Each rank shuffles the activities locally, runs a ThreadedBacktrackingSolver,
 * and obtains a local best score. The best score across all ranks is found via
 * MPI_Allreduce; the rank holding that best solution sends it to rank 0, which
 * prints and returns it. Other ranks return std::nullopt.
 */
std::optional<TimetableSolution> MPIHybridMultiStartSolver::solve(const ProblemInstance& inst) {
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Local copy so each rank can randomize activities independently.
    ProblemInstance localInst = inst;

    // Random engine seeded differently per rank for independent multi-starts.
    std::mt19937 rng(1234u + (unsigned)rank);
    std::shuffle(localInst.activities.begin(), localInst.activities.end(), rng);

    // Threaded solver inside each rank (hybrid parallelism).
    ThreadedBacktrackingSolver threadedSolver(
            /*maxSolutions=*/maxSolutions_,
            /*numThreads=*/numThreads_,
            /*frontierDepth=*/2
    );

    // Each rank computes its local best solution (if any).
    auto localOpt = threadedSolver.solve(localInst);

    int localScore = std::numeric_limits<int>::max();
    if (localOpt) {
        localScore = localOpt->score;
    }

    // Compute global best (minimum) score across all ranks.
    int globalBestScore = std::numeric_limits<int>::max();
    MPI_Allreduce(&localScore, &globalBestScore, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << "========================================\n";
        std::cout << "MPI+THREADS TIMETABLING SOLVER (MULTI-START)\n";
        std::cout << "Processes: " << size << "\n";
        std::cout << "Activities: " << inst.activities.size() << "\n";
        if (globalBestScore == std::numeric_limits<int>::max()) {
            std::cout << "No valid timetable found.\n";
        } else {
            std::cout << "Best (lowest) score across ranks: " << globalBestScore << "\n";
        }
        std::cout << "========================================\n";
    }

    // Determine which rank holds the global best solution.
    int winnerRank = -1;
    if (localScore == globalBestScore && localScore != std::numeric_limits<int>::max()) {
        winnerRank = rank;
    }
    int globalWinnerRank = -1;
    MPI_Allreduce(&winnerRank, &globalWinnerRank, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

    const int TAG_META = 300;
    const int TAG_DATA = 301;

    // Case 1: winner is rank 0 -> use localOpt directly, no send/recv.
    if (globalWinnerRank == 0) {
        if (rank == 0 && localOpt && localScore == globalBestScore) {
            TimetableSolution bestSol = *localOpt;

            std::cout << "\nBest timetable (rank 0):\n";
            std::cout << "Score = " << bestSol.score << "\n\n";
            printGroupSchedules(inst, bestSol);
            std::cout << "========================================\n";
        }
        // Rank 0 already printed the result; solver API returns std::nullopt everywhere.
        return std::nullopt;
    }

    // Case 2: winner is some non-root rank; send its solution to rank 0.
    if (rank == globalWinnerRank && localOpt) {
        const TimetableSolution& sol = *localOpt;
        std::vector<int> buf;
        serializePlacements(sol.placements, buf);
        int len = (int)buf.size();

        MPI_Send(&len, 1, MPI_INT, 0, TAG_META, MPI_COMM_WORLD);
        if (len > 0) {
            MPI_Send(buf.data(), len, MPI_INT, 0, TAG_DATA, MPI_COMM_WORLD);
        }
    }

    // Rank 0 receives and prints the best timetable from the winner rank.
    if (rank == 0 && globalWinnerRank >= 0 &&
        globalBestScore != std::numeric_limits<int>::max()) {
        int len = 0;
        MPI_Recv(&len, 1, MPI_INT, globalWinnerRank, TAG_META, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        std::vector<int> buf(len);
        if (len > 0) {
            MPI_Recv(buf.data(), len, MPI_INT, globalWinnerRank, TAG_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        std::vector<Placement> placements;
        deserializePlacements(buf, placements);

        TimetableSolution bestSol;
        bestSol.placements = std::move(placements);
        bestSol.score = globalBestScore;

        std::cout << "\nBest timetable (from rank " << globalWinnerRank << "):\n";
        std::cout << "Score = " << bestSol.score << "\n\n";
        printGroupSchedules(inst, bestSol);
        std::cout << "========================================\n";
    }

    // From an API perspective, only rank 0 "owns" the printed result;
    // solver still returns std::nullopt to all callers.
    return std::nullopt;
}
