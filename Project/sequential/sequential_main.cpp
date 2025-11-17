///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "../sequential/sequential_solver.hpp"
#include "model.hpp"
#include "formatting.hpp"
#include "demo_instances.hpp"
#include <iostream>
#include <chrono>


///////////////////////////
///     ENTRY POINT     ///
///////////////////////////
/**
 * @brief Demo entry point for the sequential timetabling solver.
 *
 * Builds a demo problem instance, runs the single-threaded backtracking solver,
 * measures its runtime, and prints both a raw and formatted view of the
 * resulting timetable if a valid solution is found.
 */
int main(int argc, char** argv) {
    // Currently no command-line handling; silence unused parameter warnings.
    (void)argc;
    (void)argv;

    // Select demo instance size (controls number of activities, groups, etc.).
    DemoSize size = DemoSize::XXL;

    // Create a synthetic problem instance for testing/benchmarking.
    ProblemInstance inst = makeDemoInstance(size);

    // Configure the sequential solver:
    //  - maxSolutions = 1 -> stop after the first best solution found.
    SequentialBacktrackingSolver seqSolver(/*maxSolutions=*/1);

    // Measure wall-clock time of the sequential search.
    auto startSeq = std::chrono::high_resolution_clock::now();
    auto seqSolutionOpt = seqSolver.solve(inst);
    auto endSeq = std::chrono::high_resolution_clock::now();
    double msSeq = std::chrono::duration<double, std::milli>(endSeq - startSeq).count();

    // High-level summary: how many activities and how long the run took.
    std::cout << "========================================\n";
    std::cout << "SEQUENTIAL TIMETABLING SOLVER\n";
    std::cout << "Activities: " << inst.activities.size() << "\n";
    std::cout << "Time: " << msSeq << " ms\n";

    // Report success or failure.
    if (!seqSolutionOpt) {
        std::cout << "No valid timetable found (sequential).\n";
    } else {
        const TimetableSolution& sol = *seqSolutionOpt;
        std::cout << "Valid timetable found (sequential), score = " << sol.score << "\n\n";

        // Low-level listing of all placements in the timetable.
        std::cout << "Raw placements (sequential):\n";
        for (const Placement& p : sol.placements) {
            if (p.activityId < 0) continue;

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

        // Higher-level, per-group visualization of the same timetable.
        std::cout << "\nPretty per-group schedules (sequential):\n";
        printGroupSchedules(inst, sol);
    }

    std::cout << "========================================\n";
    return 0;
}
