///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "formatting.hpp"
#include "demo_instances.hpp"
#include "opencl_solver.hpp"
#include <iostream>


///////////////////////////
///     ENTRY POINT     ///
///////////////////////////
/**
 * @brief Demo entry point for the OpenCL-based exhaustive timetabling solver.
 *
 * Builds a large demo instance, runs the CPU DFS + GPU scoring pipeline,
 * and prints the best timetable score and per-group schedules if a solution
 * is found.
 */
int main(int argc, char** argv) {
    // No CLI arguments are used yet; silence unused parameter warnings.
    (void)argc;
    (void)argv;

    // Use an extra-large demo to showcase GPU acceleration.
    DemoSize size = DemoSize::M;
    ProblemInstance inst = makeDemoInstance(size);

    // Configuration for the exhaustive search.
    //  - maxSolutions: upper bound on how many complete timetables to generate.
    //  - batchSize:    how many candidates to score per GPU batch.
    int maxSolutions = 1000000;
    int batchSize = 512;

    std::cout << "========================================\n";
    std::cout << "OPENCL EXHAUSTIVE TIMETABLING SOLVER\n";
    std::cout << "Activities: " << inst.activities.size() << "\n";
    std::cout << "GPU batch size: " << batchSize << "\n";
    std::cout << "========================================\n";

    // Create and run the OpenCL-based exhaustive solver.
    OpenCLExhaustiveSolver solver(maxSolutions, batchSize);
    auto solOpt = solver.solve(inst);

    // Print result summary and, if available, detailed group schedules.
    if (!solOpt) {
        std::cout << "No timetable found.\n";
    } else {
        const TimetableSolution& sol = *solOpt;
        std::cout << "Best score = " << sol.score << "\n\n";
        printGroupSchedules(inst, sol);
    }

    std::cout << "========================================\n";
    return 0;
}
