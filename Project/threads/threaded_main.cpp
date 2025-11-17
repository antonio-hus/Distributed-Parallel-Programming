///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "../threads/threaded_solver.hpp"
#include "model.hpp"
#include "formatting.hpp"
#include "demo_instances.hpp"
#include <iostream>
#include <chrono>


///////////////////////////
///     ENTRY POINT     ///
///////////////////////////
/**
 * @brief Demo entry point for the threaded timetabling solver.
 *
 * Builds a demo problem instance, runs the multithreaded backtracking solver,
 * measures its runtime, and prints both a raw and a formatted view of the
 * resulting timetable (if one is found).
 */
int main(int argc, char** argv) {
    // Suppress unused parameter warnings for now (no CLI parsing yet).
    (void)argc;
    (void)argv;

    // Choose which demo problem size to run (number of activities, etc.).
    DemoSize size = DemoSize::M;

    // Construct a synthetic problem instance with activities, rooms, groups, etc.
    ProblemInstance inst = makeDemoInstance(size);

    // Configure the threaded solver:
    //  - maxSolutions = 1  -> stop after first (best-so-far) complete solution,
    //  - numThreads   = 4  -> use four worker threads,
    //  - frontierDepth = 2 -> split search tree after assigning first 2 activities.
    int maxSolutions = 1;
    int numThreads = 4;
    int frontierDepth = 2;
    ThreadedBacktrackingSolver thrSolver(/*maxSolutions=*/maxSolutions, /*numThreads=*/numThreads, /*frontierDepth=*/frontierDepth);

    // Measure wall-clock time for the threaded solver.
    auto startThr = std::chrono::high_resolution_clock::now();
    auto thrSolutionOpt = thrSolver.solve(inst);
    auto endThr = std::chrono::high_resolution_clock::now();
    double msThr = std::chrono::duration<double, std::milli>(endThr - startThr).count();

    // High-level run summary.
    std::cout << "========================================\n";
    std::cout << "THREADED TIMETABLING SOLVER\n";
    std::cout << "Activities: " << inst.activities.size() << "\n";
    std::cout << "Time: " << msThr << " ms\n";

    // Check whether a valid timetable was found.
    if (!thrSolutionOpt) {
        std::cout << "No valid timetable found (threaded).\n";
    } else {
        const TimetableSolution& sol = *thrSolutionOpt;
        std::cout << "Valid timetable found (threaded), score = " << sol.score << "\n\n";

        // Raw, low-level listing of all placements in the solution.
        std::cout << "Raw placements (threaded):\n";
        for (const Placement& p : sol.placements) {
            if (p.activityId < 0) continue; // Skip unused entries.

            const Activity& act  = inst.activities[p.activityId];
            const Subject& subj  = inst.subjects[act.subjectId];
            const Room&    room  = inst.rooms[p.roomIndex];

            std::cout << "Activity " << p.activityId
                      << " | Subject=" << subj.name
                      << " | Prof=" << inst.professors[act.profId].name
                      << " | Day=" << p.day
                      << " Slot=" << p.slot
                      << " Room=" << room.name
                      << "\n";
        }

        // Pretty, per-group view of the timetable using helper formatting utilities.
        std::cout << "\nPretty per-group schedules (threaded):\n";
        printGroupSchedules(inst, sol);
    }

    std::cout << "========================================\n";
    return 0;
}
