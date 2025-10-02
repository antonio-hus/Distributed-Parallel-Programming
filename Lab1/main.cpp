///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>


///////////////////////////
///   STRUCTS SECTION   ///
///////////////////////////
struct Warehouse {
    // Mutex protecting concurrent access to the products map.
    // Must be acquired before reading or modifying products data.
    mutable std::mutex mtx;

    // Inventory map: stores quantity available for each product ID.
    // Key: product identifier, Value: quantity in stock
    std::unordered_map<int, long long> products;
};

struct SystemState {
    // System configuration parameters
    // Total number of warehouses in the simulation
    int numWarehouses;
    // Total number of distinct product types
    int numProducts;

    // Warehouse storage: holds pointers to all warehouse instances.
    // Using unique_ptr ensures warehouses remain at stable memory addresses.
    std::vector<std::unique_ptr<Warehouse>> warehouses;

    // Invariant validation: stores the initial total quantity for each product across all warehouses.
    // Used to verify no inventory is lost or created.
    // Index: product identifier, Value: total quantity
    std::vector<long long> initialTotals;
};

// Function pointer type for move operations
using MoveFn = bool(*)(SystemState&, int, int, const std::vector<std::pair<int, long long>>&);

struct WorkerConfig {
    // Worker thread configuration parameters
    // Thread identifier for debugging and logging
    int id;
    // Pointer to shared system state (not owned)
    SystemState* S;
    // Number of move operations this thread will perform
    int numOps;
    // Maximum number of distinct products per transaction
    int maxProductsPerMove;
    // Maximum quantity to move for a single product
    int maxDelta;
    // Random seed for reproducible test scenarios
    unsigned int rngSeed;

    // Optional verification interval: if > 0, performs inventory check
    // every k operations to detect consistency errors early
    int checkEvery;

    // Function pointer to the move strategy to use
    MoveFn moveFunction;
};

///////////////////////////
///   HELPERS SECTION   ///
///////////////////////////
/**
 * Initializes the warehouse inventory system with specified configuration.
 *
 * Creates W warehouses, each stocked with identical initial quantities of P products.
 * Also computes and stores total inventory per product for later invariant validation.
 *
 * @param S      System state to initialize (modified in place)
 * @param W      Number of warehouses to create
 * @param P      Number of distinct product types
 * @param baseQtyPerProductPerWarehouse  Initial quantity of each product in each warehouse
 */
static void init_system(SystemState& S, int W, int P, long long baseQtyPerProductPerWarehouse) {
    // Set system dimensions
    S.numWarehouses = W;
    S.numProducts = P;

    // Prepare warehouse container: clear any existing data and allocate space
    S.warehouses.clear();
    S.warehouses.reserve(W);

    // Create and initialize each warehouse with uniform product quantities
    for (int w = 0; w < W; ++w) {
        S.warehouses.push_back(std::make_unique<Warehouse>());
        Warehouse& wh = *S.warehouses[w];

        // Pre-allocate space for all products
        wh.products.reserve(P);

        // Stock warehouse with initial inventory for each product
        for (int p = 0; p < P; ++p) {
            wh.products[p] = baseQtyPerProductPerWarehouse;
        }
    }

    // Compute total inventory per product across all warehouses.
    // This baseline is used to verify conservation of inventory during operations.
    S.initialTotals.assign(P, 0);
    for (int p = 0; p < P; ++p) {
        long long tot = 0;
        for (int w = 0; w < W; ++w) {
            tot += S.warehouses[w]->products[p];
        }
        S.initialTotals[p] = tot;
    }
}

/**
 * STRATEGY 1: Hybrid Hand-Over-Hand with Ordered Locking
 *
 * Combines hand-over-hand locking with ordered lock acquisition.
 * Case 1 (src < dst): Optimal hand-over-hand with minimal lock hold time
 * Case 2 (src > dst): Must lock dst first, holds both locks briefly
 *
 * DEADLOCK AVOIDANCE: Always locks warehouses in ascending index order
 * PERFORMANCE: Best case minimal contention, moderate code complexity
 *
 * @param S       System state containing all warehouses
 * @param src     Source warehouse index
 * @param dst     Destination warehouse index
 * @param deltas  Vector of (product_id, quantity) pairs to transfer
 * @return true if the move succeeded, false if insufficient inventory
 */
static bool move_products_hybrid(SystemState& S, int src, int dst, const std::vector<std::pair<int, long long>>& deltas) {
    // Short-circuit: moving within the same warehouse is a no-op
    if (src == dst) return true;

    Warehouse& Wsrc = *S.warehouses[src];
    Warehouse& Wdst = *S.warehouses[dst];

    // DEADLOCK PREVENTION: Determine lock acquisition order based on warehouse indices.
    // By always locking in ascending order, we establish a total ordering that
    // prevents circular dependencies between threads.

    if (src < dst) {
        // Natural order: lock source first, then destination.
        // This case allows optimal hand-over-hand locking with minimal lock hold time.

        // Lock source, validate, and deduct
        Wsrc.mtx.lock();

        // Validation: verify source has sufficient quantity for ALL products.
        // All-or-nothing semantics: reject entire transaction if any product insufficient.
        for (const auto& pr : deltas) {
            int product = pr.first;
            long long d = pr.second;
            auto it = Wsrc.products.find(product);
            if (it == Wsrc.products.end() || it->second < d) {
                Wsrc.mtx.unlock();
                return false;  // Abort: insufficient inventory
            }
        }

        // Deduction: remove quantities from source.
        // Products are now "in transit" - removed from source but not yet in destination.
        for (const auto& pr : deltas) {
            int product = pr.first;
            long long d = pr.second;
            Wsrc.products[product] -= d;
        }

        // Lock destination BEFORE unlocking source to maintain transaction atomicity.
        // The reference time for the transaction is when destination is locked.
        Wdst.mtx.lock();

        // Hand-over complete; source now visible to others with deducted quantities.
        Wsrc.mtx.unlock();

        // Only destination locked; observers see consistent final state.
        for (const auto& pr : deltas) {
            int product = pr.first;
            long long d = pr.second;
            Wdst.products[product] += d;
        }

        // Transaction complete
        Wdst.mtx.unlock();

    } else {
        // Reverse order: must lock destination first to maintain index ordering.
        // This case requires holding both locks during transfer to ensure atomicity.

        // Lock destination warehouse (lower index) to respect ordering discipline.
        Wdst.mtx.lock();

        // Lock source warehouse (higher index) immediately after.
        // Both locks now held - necessary for this case to maintain atomicity.
        Wsrc.mtx.lock();

        // Validation: verify source has sufficient quantity for ALL products.
        // All-or-nothing semantics: reject entire transaction if any product insufficient.
        for (const auto& pr : deltas) {
            int product = pr.first;
            long long d = pr.second;
            auto it = Wsrc.products.find(product);
            if (it == Wsrc.products.end() || it->second < d) {
                // Both locks held - unlock in reverse acquisition order
                Wsrc.mtx.unlock();
                Wdst.mtx.unlock();

                // Abort: insufficient inventory
                return false;
            }
        }

        // With both locks held, perform the complete transfer operation.
        // This ensures atomicity: observers see either pre-transfer or post-transfer state.
        for (const auto& pr : deltas) {
            int product = pr.first;
            long long d = pr.second;
            Wsrc.products[product] -= d;
            Wdst.products[product] += d;
        }

        // Unlock destination first (maintaining reverse order pattern).
        Wdst.mtx.unlock();

        // Unlock source last. Transaction now fully visible to all threads.
        Wsrc.mtx.unlock();
    }

    return true;
}

/**
 * STRATEGY 2: Two-Point Locking with Ordered Acquisition
 *
 * Acquires both warehouse locks at start (ascending index order) and holds
 * throughout transaction. Simplest deadlock-free implementation.
 *
 * DEADLOCK AVOIDANCE: Consistent lock ordering, no circular waits
 * PERFORMANCE: Higher contention but simplest code, single execution path
 *
 * @param S       System state containing all warehouses
 * @param src     Source warehouse index
 * @param dst     Destination warehouse index
 * @param deltas  Vector of (product_id, quantity) pairs to transfer
 * @return true if the move succeeded, false if insufficient inventory
 */
static bool move_products_two_point(SystemState& S, int src, int dst, const std::vector<std::pair<int, long long>>& deltas) {
    // Short-circuit: moving within the same warehouse is a no-op
    if (src == dst) return true;

    Warehouse& Wsrc = *S.warehouses[src];
    Warehouse& Wdst = *S.warehouses[dst];

    // DEADLOCK PREVENTION: Always lock in ascending index order.
    // Determine which warehouse to lock first based on indices.
    int first_idx = std::min(src, dst);
    int second_idx = std::max(src, dst);
    Warehouse& Wfirst = *S.warehouses[first_idx];
    Warehouse& Wsecond = *S.warehouses[second_idx];

    // Lock lower index warehouse first to establish total ordering.
    Wfirst.mtx.lock();

    // Lock higher index warehouse second.
    // Both locks now held - transaction executes in single critical section.
    Wsecond.mtx.lock();

    // Verify source warehouse has sufficient inventory for ALL products.
    // All-or-nothing semantics: reject entire transaction if any product insufficient.
    for (const auto& pr : deltas) {
        int product = pr.first;
        long long d = pr.second;
        auto it = Wsrc.products.find(product);
        if (it == Wsrc.products.end() || it->second < d) {
            // Abort transaction: unlock in reverse acquisition order
            Wsecond.mtx.unlock();
            Wfirst.mtx.unlock();
            return false;  // Insufficient inventory
        }
    }

    // Deduct quantities from source warehouse.
    // Products transition from source to destination atomically.
    for (const auto& pr : deltas) {
        int product = pr.first;
        long long d = pr.second;
        Wsrc.products[product] -= d;
    }

    // Add quantities to destination warehouse.
    // Transfer now complete within the critical section.
    for (const auto& pr : deltas) {
        int product = pr.first;
        long long d = pr.second;
        Wdst.products[product] += d;
    }

    // Unlock in reverse acquisition order (LIFO discipline).
    // Order doesn't affect correctness here but maintains consistent pattern.
    Wsecond.mtx.unlock();
    Wfirst.mtx.unlock();
    return true;
}

/**
 * STRATEGY 3: Hand-Over-Hand Locking (Original - Deadlock Prone)
 *
 * Locks source first, modifies it, then acquires destination before releasing
 * source. Minimal lock hold time but vulnerable to deadlocks with bidirectional
 * concurrent transfers.
 *
 * WARNING: Can deadlock when Thread A does (wh1→wh2) while Thread B does (wh2→wh1)
 *
 * @param S       System state containing all warehouses
 * @param src     Source warehouse index
 * @param dst     Destination warehouse index
 * @param deltas  Vector of (product_id, quantity) pairs to transfer
 * @return true if the move succeeded, false if insufficient inventory
 */
static bool move_products_hand_over(SystemState& S, int src, int dst, const std::vector<std::pair<int, long long>>& deltas) {
    // Short-circuit: moving within the same warehouse is a no-op
    if (src == dst) return true;

    Warehouse& Wsrc = *S.warehouses[src];
    Warehouse& Wdst = *S.warehouses[dst];

    // Lock source, validate, and deduct
    Wsrc.mtx.lock();

    // Validation: verify source has sufficient quantity for ALL products.
    // All-or-nothing semantics: reject entire transaction if any product insufficient.
    for (const auto& pr : deltas) {
        int product = pr.first;
        long long d = pr.second;
        auto it = Wsrc.products.find(product);
        if (it == Wsrc.products.end() || it->second < d) {
            Wsrc.mtx.unlock();
            return false;  // Abort: insufficient inventory
        }
    }

    // Deduction: remove quantities from source.
    // Products are now "in transit" - removed from source but not yet in destination.
    for (const auto& pr : deltas) {
        int product = pr.first;
        long long d = pr.second;
        Wsrc.products[product] -= d;
    }

    // Lock destination BEFORE unlocking source to maintain transaction atomicity.
    // The reference time for the transaction is when destination is locked.
    Wdst.mtx.lock();

    // DEADLOCK RISK: If another thread is transferring dst→src and has locked
    // dst while waiting for src, a circular dependency forms:
    // -  This thread: holds src, wants dst
    // -  Other thread: holds dst, wants src
    // -  Result: Both threads wait forever (deadlock)

    // Hand-over complete; source now visible to others with deducted quantities.
    Wsrc.mtx.unlock();

    // Only destination locked; observers see consistent final state.
    for (const auto& pr : deltas) {
        int product = pr.first;
        long long d = pr.second;
        Wdst.products[product] += d;
    }

    // Transaction complete
    Wdst.mtx.unlock();
    return true;
}

/**
 * Validates the inventory conservation invariant across all warehouses.
 *
 * Verifies that the total quantity of each product across all warehouses matches
 * the initial baseline. This detects bugs like inventory leaks, duplication, or
 * race conditions in move operations.
 *
 * Uses fine-grained locking (one warehouse at a time) to minimize contention and
 * allow concurrent move operations to proceed during validation. Note: this creates
 * a snapshot consistency model where the check sees a mix of states across warehouses.
 *
 * @param S  System state to validate (read-only)
 * @return true if all products maintain their initial total quantities, false otherwise
 */
static bool inventory_check(const SystemState& S) {
    // Iterate through each product type and sum its total quantity
    for (int p = 0; p < S.numProducts; ++p) {
        long long tot = 0;

        // Fine-grained locking strategy: lock each warehouse briefly, accumulate
        // its contribution, then release. This avoids holding multiple locks
        // simultaneously, reducing lock contention and allowing concurrent moves.
        for (int w = 0; w < S.numWarehouses; ++w) {
            const Warehouse& wh = *S.warehouses[w];

            // Acquire warehouse lock for minimal duration (read product quantity only).
            // Mutex ensures we read consistent data even if moves are in progress.
            std::unique_lock<std::mutex> lk(wh.mtx);
            auto it = wh.products.find(p);
            if (it != wh.products.end()) {
                tot += it->second;
            }
            // Lock automatically released here (end of scope), allowing other
            // threads to access this warehouse immediately
        }

        // Verify conservation: total quantity must equal initial baseline.
        // Deviation indicates a bug in the transfer logic.
        if (tot != S.initialTotals[p]) {
            // Invariant violated - inventory leaked or duplicated
            return false;
        }
    }

    // All products passed validation
    return true;
}

/**
 * Worker thread function: simulates concurrent warehouse operations.
 *
 * Each thread repeatedly performs random product transfers between warehouses,
 * stressing the concurrency control mechanisms and testing for race conditions.
 * Uses a seeded RNG for reproducible test scenarios.
 *
 * @param cfg  Configuration specifying thread behavior, operation count, and system state
 */
static void worker_thread(WorkerConfig cfg) {
    // Initialize thread-local random number generator with unique seed.
    // Each thread gets its own RNG to avoid contention on shared random state.
    std::mt19937 rng(cfg.rngSeed);
    std::uniform_int_distribution<int> whDist(0, cfg.S->numWarehouses - 1);
    std::uniform_int_distribution<int> prodDist(0, cfg.S->numProducts - 1);
    std::uniform_int_distribution<int> cntDist(1, cfg.maxProductsPerMove);
    std::uniform_int_distribution<int> deltaDist(1, cfg.maxDelta);

    // Execute the configured number of move operations
    for (int i = 0; i < cfg.numOps; ++i) {
        // Select random source and destination warehouses (must be different)
        int src = whDist(rng);
        int dst = whDist(rng);
        while (dst == src) dst = whDist(rng);

        // Determine how many distinct products to include in this transaction
        int count = cntDist(rng);

        // Build a set of distinct products for this move.
        // Distinct products reduce lock contention - multiple threads can move
        // disjoint product sets between the same warehouse pair concurrently.
        std::vector<int> chosen;
        chosen.reserve(count);
        for (int k = 0; k < count; ++k) {
            int p = prodDist(rng);

            // Ensure uniqueness: retry if product already selected.
            // Guard prevents infinite loop in edge cases (e.g., count > numProducts)
            int guard = 0;
            while (std::find(chosen.begin(), chosen.end(), p) != chosen.end()) {
                p = prodDist(rng);

                // Give up after 10 attempts
                if (++guard > 10) break;
            }

            // Add product if it's unique (or we've exhausted retry attempts)
            if (std::find(chosen.begin(), chosen.end(), p) == chosen.end())
                chosen.push_back(p);
        }

        // Build transaction: pair each product with a random quantity to transfer
        std::vector<std::pair<int, long long>> deltas;
        deltas.reserve(chosen.size());
        for (int p : chosen) {
            deltas.emplace_back(p, static_cast<long long>(deltaDist(rng)));
        }

        // Attempt the move operation using the configured strategy.
        // Returns false if source warehouse lacks sufficient inventory,
        // which is expected behavior - just skip and continue.
        (void)cfg.moveFunction(*cfg.S, src, dst, deltas);

        // Periodic invariant validation (if enabled).
        // Runs inventory check every k operations to detect consistency bugs early
        // during stress testing. Disabled by default (checkEvery = 0) for performance.
        if (cfg.checkEvery > 0 && (i % cfg.checkEvery == 0)) {
            // inventory_check uses fine-grained locking to avoid blocking moves.
            // Safe to call concurrently from multiple threads.
            (void)inventory_check(*cfg.S);
        }
    }
}

/**
 * Runs a benchmark with specified move strategy and configuration.
 *
 * @param strategyName  Human-readable name of the strategy
 * @param moveFn        Function pointer to the move strategy
 * @param S             System state (will be reinitialized)
 * @param W             Number of warehouses
 * @param P             Number of products
 * @param perWhPerProd  Initial quantity per product per warehouse
 * @param numThreads    Number of worker threads
 * @param opsPerThread  Operations per thread
 * @param maxProductsPerMove  Maximum products per transaction
 * @param maxDelta      Maximum quantity per product move
 * @param checkEvery    Intermediate check interval (0 = disabled)
 */
static void run_benchmark(
        const char* strategyName,
        MoveFn moveFn,
        SystemState& S,
        int W, int P, long long perWhPerProd,
        int numThreads, int opsPerThread,
        int maxProductsPerMove, int maxDelta,
        int checkEvery
) {
    std::cout << "\n========================================\n";
    std::cout << "Testing Strategy: " << strategyName << "\n";
    std::cout << "========================================\n";

    // Reinitialize system for clean test
    init_system(S, W, P, perWhPerProd);

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numThreads; ++i) {
        WorkerConfig cfg{
                .id = i,
                .S = &S,
                .numOps = opsPerThread,
                .maxProductsPerMove = maxProductsPerMove,
                .maxDelta = maxDelta,
                .rngSeed = static_cast<unsigned int>(std::random_device{}() ^ (i * 0x9e3779b9U)),
                .checkEvery = checkEvery,
                .moveFunction = moveFn
        };
        threads.emplace_back(worker_thread, cfg);
    }

    for (auto& th : threads) th.join();

    auto t1 = std::chrono::high_resolution_clock::now();

    bool ok = inventory_check(S);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::cout << "Result: " << (ok ? "PASS" : "FAIL") << " - Inventory invariant "
              << (ok ? "preserved" : "BROKEN") << "\n";
    std::cout << "Elapsed time: " << ms << " ms\n";
    std::cout << "Total operations: " << (numThreads * opsPerThread) << "\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2)
              << (numThreads * opsPerThread * 1000.0 / ms) << " ops/sec\n";
}


/////////////////////////
///   MAIN SECTION    ///
/////////////////////////
/**
 * Main entry point: Concurrent warehouse inventory stress test.
 *
 * Simulates multiple threads performing simultaneous product transfers between
 * warehouses to validate thread-safety of the inventory management system.
 * Tests all three locking strategies and compares their performance.
 */
int main() {
    // Display hardware information
    std::cout << "========================================\n";
    std::cout << "HARDWARE CONFIGURATION\n";
    std::cout << "========================================\n";
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << " threads\n";
    std::cout << "Pointer size: " << (sizeof(void*) * 8) << "-bit\n";

    // ========== Configuration Presets ==========
    // Uncomment one of the presets below or customize parameters manually

    // PRESET 1: Small scale - Quick validation test
    // int W = 4, P = 16, perWhPerProd = 100, numThreads = 2, opsPerThread = 1000;
    // int maxProductsPerMove = 2, maxDelta = 3, intermediateCheckEvery = 0;

    // PRESET 2: Medium scale - Standard stress test
    // int W = 16, P = 256, perWhPerProd = 1000, numThreads = 8, opsPerThread = 20000;
    // int maxProductsPerMove = 4, maxDelta = 5, intermediateCheckEvery = 0;

    // PRESET 3: Large scale - High contention test
    // int W = 32, P = 512, perWhPerProd = 5000, numThreads = 16, opsPerThread = 50000;
    // int maxProductsPerMove = 8, maxDelta = 10, intermediateCheckEvery = 0;

    // PRESET 4: Fine-grained - Low contention test
    int W = 64, P = 1024, perWhPerProd = 500, numThreads = 16, opsPerThread = 100000;
    int maxProductsPerMove = 2, maxDelta = 2, intermediateCheckEvery = 0;

    std::cout << "\n========================================\n";
    std::cout << "TEST CONFIGURATION\n";
    std::cout << "========================================\n";
    std::cout << "Warehouses: " << W << "\n";
    std::cout << "Products: " << P << "\n";
    std::cout << "Initial qty/product/warehouse: " << perWhPerProd << "\n";
    std::cout << "Worker threads: " << numThreads << "\n";
    std::cout << "Operations per thread: " << opsPerThread << "\n";
    std::cout << "Max products per move: " << maxProductsPerMove << "\n";
    std::cout << "Max quantity per product: " << maxDelta << "\n";

    SystemState S{};

    // Test all three strategies
    run_benchmark(
            "Hybrid Hand-Over-Hand",
            move_products_hybrid,
            S, W, P, perWhPerProd, numThreads, opsPerThread,
            maxProductsPerMove, maxDelta, intermediateCheckEvery
    );

    run_benchmark(
            "Two-Point Locking",
            move_products_two_point,
            S, W, P, perWhPerProd, numThreads, opsPerThread,
            maxProductsPerMove, maxDelta, intermediateCheckEvery
    );

    run_benchmark(
        "Hand-Over-Hand (Deadlock Prone)",
        move_products_hand_over,
        S, W, P, perWhPerProd, numThreads, opsPerThread,
        maxProductsPerMove, maxDelta, intermediateCheckEvery
    );

    std::cout << "\n========================================\n";
    std::cout << "ALL TESTS COMPLETED\n";
    std::cout << "========================================\n";

    return 0;
}