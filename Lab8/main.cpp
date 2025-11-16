#include "dsm.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

// Event logging for verification
struct EventLog {
    int rank;
    int variable_id;
    int old_value;
    int new_value;
    int lamport_time;
};

std::vector<EventLog> event_sequence;

void runScenario1(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] === Scenario 1: Sequential Consistency ===\n";

    // All ranks subscribe to var 0; ranks 0,1,2 write it to create contention
    if (rank == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        dsm.write(0, 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        dsm.write(0, 200);
    } else if (rank == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        dsm.write(0, 150);
    } else if (rank == 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        dsm.write(0, 120);
    }

    for (int i = 0; i < 15; ++i) {
        dsm.processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void runScenario2(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] === Scenario 2: Multiple Variables & Local Groups ===\n";

    // var 1: all ranks subscribe and can write.
    // var 2: only ranks {0,2,3} subscribe and are allowed to write it.

    if (rank == 0) {
        dsm.write(1, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        dsm.write(2, 20);   // allowed (rank 0 subscribes to var 2)
    } else if (rank == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        dsm.write(1, 15);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // rank 1 does NOT write var 2, to respect subscription set
    } else if (rank == 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        dsm.write(1, 12);
        dsm.write(2, 22);   // allowed (rank 2 subscribes to var 2)
    }
    // rank 3 does not write, but sees all changes it subscribes to

    for (int i = 0; i < 15; ++i) {
        dsm.processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void runScenario3(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] === Scenario 3: CAS Atomicity ===\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(50 * rank));
    bool success = dsm.compareAndSwap(3, 0, rank * 10);
    std::cout << "[Rank " << rank << "] CAS(3, 0, " << (rank * 10)
              << ") = " << (success ? "SUCCESS" : "FAILED") << "\n";

    for (int i = 0; i < 10; ++i) {
        dsm.processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (rank == 0) {
        int current_value = dsm.read(3);
        std::cout << "[Rank 0] Current value: " << current_value << "\n";
        success = dsm.compareAndSwap(3, current_value, 999);
        std::cout << "[Rank 0] CAS(3, " << current_value
                  << ", 999) = " << (success ? "SUCCESS" : "FAILED") << "\n";
    }

    for (int i = 0; i < 10; ++i) {
        dsm.processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void runScenario4(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] === Scenario 4: Happens-Before Relations ===\n";

    if (rank == 0) {
        dsm.write(4, 1);
        std::cout << "[Rank 0] Event A: write(4, 1) | Clock=" << dsm.getLamportClock() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        dsm.write(4, 2);
        std::cout << "[Rank 0] Event B: write(4, 2) | Clock=" << dsm.getLamportClock() << "\n";
    } else if (rank == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        dsm.write(4, 3);
        std::cout << "[Rank 1] Event C: write(4, 3) | Clock=" << dsm.getLamportClock() << "\n";
    }

    for (int i = 0; i < 15; ++i) {
        dsm.processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Only variables 0,1,3,4 are subscribed by all processes; 2 is local-group only.
static bool is_globally_shared_var(int var_id) {
    return var_id == 0 || var_id == 1 || var_id == 3 || var_id == 4;
}

void verifySequentialConsistency(int rank, int world_size) {
    // Count only callbacks for globally shared variables (0,1,3,4)
    int local_event_count = 0;
    for (const auto& e : event_sequence) {
        if (is_globally_shared_var(e.variable_id)) {
            local_event_count++;
        }
    }

    std::vector<int> all_counts(world_size);

    MPI_Gather(&local_event_count, 1, MPI_INT,
               all_counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << "\n========================================\n";
        std::cout << "SEQUENTIAL CONSISTENCY VERIFICATION\n";
        std::cout << "========================================\n";
        std::cout << "Event counts per process (vars 0,1,3,4 only):\n";
        for (int i = 0; i < world_size; ++i) {
            std::cout << "  Rank " << i << ": " << all_counts[i] << " events\n";
        }

        bool consistent = true;
        for (int i = 1; i < world_size; ++i) {
            if (all_counts[i] != all_counts[0]) {
                consistent = false;
                break;
            }
        }

        std::cout << " Consistency: "
                  << (consistent ? "PASSED" : "FAILED") << "\n";

        std::cout << "\nEvent sequence on Rank 0 (vars 0,1,3,4):\n";
        for (const auto& e : event_sequence) {
            if (!is_globally_shared_var(e.variable_id)) continue;
            std::cout << "  [T=" << e.lamport_time << "] Var " << e.variable_id
                      << ": " << e.old_value << " -> " << e.new_value
                      << " (rank " << e.rank << ")\n";
        }
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size < 3) {
        if (rank == 0) {
            std::cerr << "Error: Requires at least 3 processes\n";
            std::cerr << "Run: mpiexec -n 4 Lab8_DSM.exe\n";
        }
        MPI_Finalize();
        return 1;
    }

    // For submission, verbose=false to keep output clean; set true while debugging if needed.
    DistributedSharedMemory dsm(rank, world_size, /*verbose=*/false);

    // Subscription sets:
    //   - var 0,1,3,4: all ranks subscribe.
    //   - var 2: only ranks {0,2,3} subscribe, to illustrate local groups.
    std::set<int> all_processes;
    for (int i = 0; i < world_size; ++i) {
        all_processes.insert(i);
    }

    std::set<int> subs_var2 = {0, 2, 3};

    dsm.subscribe(0, all_processes);
    dsm.subscribe(1, all_processes);
    if (subs_var2.count(rank) != 0) {
        dsm.subscribe(2, subs_var2);   // only ranks 0,2,3 subscribe to var 2
    }
    dsm.subscribe(3, all_processes);
    dsm.subscribe(4, all_processes);

    dsm.setChangeCallback([rank](int var_id, int old_val, int new_val, int lamport_time) {
        std::cout << "[Rank " << rank << "] CALLBACK: Var " << var_id
                  << ": " << old_val << " -> " << new_val
                  << " | T=" << lamport_time << "\n" << std::flush;
        event_sequence.push_back({rank, var_id, old_val, new_val, lamport_time});
    });

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << "\n=============================================\n";
        std::cout << "DISTRIBUTED SHARED MEMORY (NO SEQUENCER)\n";
        std::cout << "Lamport total-order multicast\n";
        std::cout << "Processes: " << world_size << "\n";
        std::cout << "Variables: 0-4\n";
        std::cout << "=============================================\n\n";
    }

    MPI_Barrier(MPI_COMM_WORLD);

    runScenario1(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runScenario2(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runScenario3(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runScenario4(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    verifySequentialConsistency(rank, world_size);

    if (rank == 0) {
        std::cout << "\n========================================\n";
        std::cout << "Final Values (Rank 0):\n";
        for (int i = 0; i <= 4; ++i) {
            // read() also advances the clock but that's fine for a final print.
            std::cout << "  Variable " << i << ": " << dsm.read(i) << "\n";
        }
        std::cout << "  Lamport Clock: " << dsm.getLamportClock() << "\n";
        std::cout << "========================================\n";
    }

    MPI_Finalize();
    return 0;
}
