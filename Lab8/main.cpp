#include "dsm.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <mpi.h>

struct EventLog {
    int rank;
    int variable_id;
    int old_value;
    int new_value;
    int lamport_time;
};
std::vector<EventLog> event_sequence;

void runScenario1(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] Scenario 1: Sequential Consistency\n" << std::flush;
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
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void runPartialSubscriptionScenario(DistributedSharedMemory& dsm, int rank) {
    // Shows that only subscribers of var2 {0,2,3} may write/read/callback
    std::cout << "[Rank " << rank << "] Scenario 2: Partial Subscription (local group for var2)\n" << std::flush;
    // Only these ranks subscribe to var 2; others (like 1) are NOT allowed to write!
    if (rank == 0) {
        dsm.write(2, 42);
    } else if (rank == 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        dsm.write(2, 84);
    } else if (rank == 3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        dsm.write(2, 168);
    }
    // Rank 1 cannot write: not subscribed, would throw if attempted!
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void runGlobalVarsScenario(DistributedSharedMemory& dsm, int rank) {
    // var 1 is global, all can write
    if (rank == 1) {
        dsm.write(1, 10);
    } else if (rank == 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
        dsm.write(1, 20);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void runScenario3(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] Scenario 3: CAS Atomicity\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(35 * rank));
    bool success = dsm.compareAndSwap(3, 0, 20 + 10*rank);
    std::cout << "[Rank " << rank << "] CAS(3, 0, " << (20 + 10*rank) << ") = " << (success ? "SUCCESS" : "FAILED") << "\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (rank == 2) {
        int v = dsm.read(3);
        std::cout << "[Rank 2] After first CAS, var 3 = " << v << "\n" << std::flush;
        success = dsm.compareAndSwap(3, v, 777);
        std::cout << "[Rank 2] CAS(3, " << v << ", 777) = " << (success ? "SUCCESS" : "FAILED") << "\n" << std::flush;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void runScenario4(DistributedSharedMemory& dsm, int rank) {
    std::cout << "[Rank " << rank << "] Scenario 4: Happens-Before\n" << std::flush;
    if (rank == 0) {
        dsm.write(4, 1);
        std::cout << "[Rank 0] Event A: write(4, 1) | Clock=" << dsm.getLamportClock() << "\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        dsm.write(4, 2);
        std::cout << "[Rank 0] Event B: write(4, 2) | Clock=" << dsm.getLamportClock() << "\n" << std::flush;
    } else if (rank == 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        dsm.write(4, 3);
        std::cout << "[Rank 1] Event C: write(4, 3) | Clock=" << dsm.getLamportClock() << "\n" << std::flush;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

static bool is_globally_shared_var(int var_id) {
    return var_id == 0 || var_id == 1 || var_id == 3 || var_id == 4;
}
void verifySequentialConsistency(int rank, int world_size) {
    int local_event_count = 0;
    for (const auto& e : event_sequence)
        if (is_globally_shared_var(e.variable_id)) local_event_count++;

    std::vector<int> all_counts(world_size);
    MPI_Gather(&local_event_count, 1, MPI_INT, all_counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << "\n=== SEQUENTIAL CONSISTENCY CHECK ===\n";
        for (int i = 0; i < world_size; ++i)
            std::cout << "Rank " << i << ": " << all_counts[i] << " events\n";
        bool consistent = true;
        for (int i = 1; i < world_size; ++i)
            if (all_counts[i] != all_counts[0]) consistent = false;
        std::cout << (consistent ? "Consistency: PASSED\n" : "Consistency: FAILED\n");
        std::cout << "Event sequence on rank 0 (globals):\n";
        for (const auto& e : event_sequence)
            if (is_globally_shared_var(e.variable_id))
                std::cout << " [T=" << e.lamport_time << "] Var " << e.variable_id
                          << ": " << e.old_value << " -> " << e.new_value
                          << " (rank " << e.rank << ")\n";
        std::cout << "====================================\n";
    }
}

int main(int argc, char** argv) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size != NUM_PROCESSES) {
        if (rank == 0)
            std::cerr << "Error: Requires exactly " << NUM_PROCESSES << " MPI processes!\n";
        MPI_Finalize();
        return 1;
    }
    DistributedSharedMemory dsm(rank, world_size, false);

    std::set<int> all_ranks;
    for (int i = 0; i < world_size; ++i) all_ranks.insert(i);
    std::set<int> subs_var2 = {0, 2, 3};
    dsm.subscribe(0, all_ranks);
    dsm.subscribe(1, all_ranks);
    if (subs_var2.count(rank)) dsm.subscribe(2, subs_var2);
    dsm.subscribe(3, all_ranks);
    dsm.subscribe(4, all_ranks);

    dsm.setChangeCallback([rank](int var_id, int old_val, int new_val, int lamport_time) {
        std::cout << "[Rank " << rank << "] CALLBACK: Var " << var_id
                  << ": " << old_val << " -> " << new_val
                  << " | T=" << lamport_time << "\n" << std::flush;
        event_sequence.push_back({rank, var_id, old_val, new_val, lamport_time});
    });

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "\n=============================================\n";
        std::cout << "DISTRIBUTED SHARED MEMORY — LAB 8\n";
        std::cout << "Lamport Total-Order Multicast, Replicated Model\n";
        std::cout << "Processes: " << world_size << "\n";
        std::cout << "Variables per process: " << NUM_VARIABLES << "\n";
        std::cout << "=============================================\n\n";
    }
    MPI_Barrier(MPI_COMM_WORLD);

    runScenario1(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runPartialSubscriptionScenario(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runGlobalVarsScenario(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runScenario3(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    runScenario4(dsm, rank);
    MPI_Barrier(MPI_COMM_WORLD);

    verifySequentialConsistency(rank, world_size);

    if (rank == 0) {
        std::cout << "\n=== Final Variable Values (Rank 0 view): ===\n";
        for (int i = 0; i < NUM_VARIABLES; ++i)
            std::cout << "  Variable " << i << ": " << dsm.read(i) << "\n";
        std::cout << "  Lamport Clock: " << dsm.getLamportClock() << "\n";
        std::cout << "=============================================\n";
    }

    MPI_Finalize();
    return 0;
}
