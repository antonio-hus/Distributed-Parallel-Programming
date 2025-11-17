///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "formatting.hpp"
#include "demo_instances.hpp"
#include "opencl_solver.hpp"
#include <iostream>
#include <chrono>

///////////////////////////
///     ENTRY POINT     ///
///////////////////////////
/**
 * @brief Demo entry point for the OpenCL-based exhaustive timetabling solver.
 *
 * Builds a demo instance, runs the CPU DFS + GPU scoring pipeline,
 * and prints the best timetable score and per-group schedules if a solution
 * is found.
 */
int main(int argc, char** argv) {
    // No CLI arguments are used yet; silence unused parameter warnings.
    (void)argc;
    (void)argv;

    // Choose which demo problem size to run.
    DemoSize size = DemoSize::XXL;
    ProblemInstance inst = makeDemoInstance(size);

    // Configuration for the exhaustive search.
    //  - maxSolutions: upper bound on how many complete timetables to generate.
    //  - batchSize:    how many candidates to score per GPU batch.
    int maxSolutions = 1;
    int batchSize    = 512;

    std::cout << "========================================\n";
    std::cout << "OPENCL EXHAUSTIVE TIMETABLING SOLVER\n";
    std::cout << "Activities: " << inst.activities.size() << "\n";
    std::cout << "GPU batch size: " << batchSize << "\n";
    std::cout << "========================================\n";

    OpenCLExhaustiveSolver solver(maxSolutions, batchSize);

    // Measure wall-clock time for the OpenCL solver.
    auto start = std::chrono::high_resolution_clock::now();
    auto solOpt = solver.solve(inst);
    auto end   = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "OpenCL solver time: " << elapsedMs << " ms\n";

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
