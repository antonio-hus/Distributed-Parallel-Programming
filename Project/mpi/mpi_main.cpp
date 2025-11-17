///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "mpi_solver.hpp"
#include "demo_instances.hpp"
#include <mpi.h>
#include <iostream>

///////////////////////////
///     ENTRY POINT     ///
///////////////////////////

/**
 * @brief MPI entry point for the hybrid MPI + threads timetabling demo.
 *
 * Initializes MPI, constructs a demo problem instance on each rank, runs the
 * MPIHybridMultiStartSolver, and finalizes MPI. Rank 0 prints high-level run
 * information and the best timetable found across all ranks.
 */
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Only rank 0 prints a brief header about the MPI configuration.
    if (rank == 0) {
        std::cout << "========================================\n";
        std::cout << "MPI+THREADS TIMETABLING SOLVER\n";
        std::cout << "Processes: " << size << "\n";
        std::cout << "========================================\n";
    }

    // Use a medium-size synthetic problem instance for distributed tests.
    DemoSize demoSize = DemoSize::M;
    ProblemInstance inst = makeDemoInstance(demoSize);

    // Hybrid solver:
    //  - maxSolutions: per-rank limit of solutions explored,
    //  - numThreads:   threads used inside each MPI process.
    int maxSolutions = 1000000;
    int numThreads = 16;
    MPIHybridMultiStartSolver solver(/*maxSolutions=*/maxSolutions, /*numThreads=*/numThreads);

    // All ranks participate; rank 0 will print the best result.
    solver.solve(inst);

    MPI_Finalize();
    return 0;
}
