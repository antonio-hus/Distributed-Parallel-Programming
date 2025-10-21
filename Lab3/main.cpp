///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>


///////////////////////////
///   STRUCTS SECTION   ///
///////////////////////////

/**
 * Matrix structure: Simple 2D matrix.
 *
 * Memory layout: rows stored contiguously, providing good cache locality
 * when accessing elements in the same row sequentially.
 */
struct Matrix {
    // Matrix dimensions
    int rows;
    int cols;

    // Data storage: flat vector for cache-friendly access
    // Element at (i, j) stored at index: i * cols + j
    std::vector<int> data;

    /**
     * Constructor: Allocates matrix storage and initializes to zero.
     *
     * @param r  Number of rows
     * @param c  Number of columns
     */
    Matrix(int r, int c) : rows(r), cols(c) {
        data.resize(rows * cols, 0);
    }

    /**
     * Element access: Returns reference to element at position (row, col).
     *
     * Uses row-major indexing for contiguous memory access within rows.
     *
     * @param row  Row index (0-based)
     * @param col  Column index (0-based)
     * @return     Reference to element at specified position
     */
    int& at(int row, int col) {
        return data[row * cols + col];
    }

    /**
     * Const element access: Read-only version for const matrices.
     *
     * @param row  Row index (0-based)
     * @param col  Column index (0-based)
     * @return     Const reference to element
     */
    const int& at(int row, int col) const {
        return data[row * cols + col];
    }

    /**
     * Random initialization: Fills matrix with random integers.
     *
     * Uses uniform distribution over specified range for test data generation.
     *
     * @param min_val  Minimum value (inclusive)
     * @param max_val  Maximum value (inclusive)
     */
    void randomize(int min_val = 1, int max_val = 10) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(min_val, max_val);

        for (int i = 0; i < rows * cols; ++i) {
            data[i] = dist(gen);
        }
    }

    /**
     * Display: Prints matrix in formatted grid layout.
     */
    void print() const {
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                std::cout << std::setw(6) << at(i, j) << " ";
            }
            std::cout << "\n";
        }
    }
};


/**
 * Thread configuration: Parameters for worker thread execution.
 *
 * Each thread receives its own configuration specifying which portion
 * of the result matrix to compute. No shared state modification occurs,
 * eliminating need for synchronization primitives.
 */
struct ThreadConfig {
    // Thread identifier for debugging output
    int thread_id;

    // Input matrices: read-only access (no mutations)
    const Matrix* A;
    const Matrix* B;

    // Output matrix: each thread writes to disjoint regions
    Matrix* C;

    // Work distribution: defines which elements this thread computes
    // Interpretation depends on distribution strategy
    int start_idx;
    int end_idx;
};


///////////////////////////
///   CORE FUNCTIONS    ///
///////////////////////////

/**
 * Element computation: Calculates single element of result matrix C = A x B.
 *
 * Computes C[row][col] using dot product of A's row with B's column.
 * This is the fundamental operation - each call is independent and requires
 * no synchronization since different threads compute different output elements.
 *
 * Formula: C[i][j] = Σ(k=0 to n-1) A[i][k] x B[k][j]
 *
 * @param A          Left operand matrix (dimensions: m x n)
 * @param B          Right operand matrix (dimensions: n x p)
 * @param row        Row index in result matrix C
 * @param col        Column index in result matrix C
 * @param thread_id  Identifier of calling thread (for debug output)
 * @return           Computed element value for C[row][col]
 */
static int compute_element(const Matrix& A, const Matrix& B, int row, int col, int thread_id) {
    // Debug output: shows which thread computes each element
    // std::cout << "Thread " << thread_id << ": Computing element (" << row << ", " << col << ")\n";

    // Dot product accumulator
    int sum = 0;

    // Multiply corresponding elements from A's row and B's column
    // Inner dimension (A.cols == B.rows) must match for valid multiplication
    for (int k = 0; k < A.cols; ++k) {
        sum += A.at(row, k) * B.at(k, col);
    }

    return sum;
}


/**
 * STRATEGY 1: Row-by-Row Distribution
 *
 * Work division: Threads process consecutive elements in row-major order.
 * Each thread gets approximately equal number of elements, distributed
 * sequentially across rows.
 *
 * Example (9x9 result, 4 threads):
 * - Thread 0: rows 0-1, elements 0-1 of row 2 (20 elements)
 * - Thread 1: remainder of row 2, row 3, elements 0-3 of row 4 (20 elements)
 * - Thread 2: remainder of row 4, row 5, elements 0-5 of row 6 (20 elements)
 * - Thread 3: remainder of row 6, rows 7-8 (21 elements)
 *
 * Cache behavior: EXCELLENT
 * - Sequential row access provides optimal spatial locality
 * - Matrix A: rows accessed contiguously (cache-friendly)
 * - Matrix B: columns accessed with stride (moderate cache misses)
 * - Result C: written sequentially (cache-friendly)
 *
 * Thread safety: GUARANTEED
 * - Each thread writes to completely disjoint memory regions
 * - No overlap in output indices between threads
 * - No synchronization primitives required
 *
 * @param cfg  Thread configuration specifying input/output matrices and work range
 */
static void strategy_row_by_row(const ThreadConfig& cfg) {
    const int total_elements = cfg.C->rows * cfg.C->cols;

    // Iterate through assigned range of linear indices
    for (int idx = cfg.start_idx; idx < cfg.end_idx && idx < total_elements; ++idx) {
        // Convert linear index to 2D coordinates (row-major order)
        // Formula: idx = row x cols + col
        int row = idx / cfg.C->cols;
        int col = idx % cfg.C->cols;

        // Compute and store result element
        // Direct write to output matrix - no race condition since indices are unique
        cfg.C->at(row, col) = compute_element(*cfg.A, *cfg.B, row, col, cfg.thread_id);
    }
}


/**
 * STRATEGY 2: Column-by-Column Distribution
 *
 * Work division: Threads process consecutive elements in column-major order.
 * Elements assigned sequentially down columns, then across to next column.
 *
 * Example (9x9 result, 4 threads):
 * - Thread 0: columns 0-1, elements 0-1 of column 2 (20 elements)
 * - Thread 1: remainder of column 2, column 3, elements 0-3 of column 4 (20 elements)
 * - Thread 2: remainder of column 4, column 5, elements 0-5 of column 6 (20 elements)
 * - Thread 3: remainder of column 6, columns 7-8 (21 elements)
 *
 * Cache behavior: POOR
 * - Column-major access on row-major storage causes strided memory access
 * - Matrix A: rows accessed with large strides (many cache misses)
 * - Matrix B: columns accessed sequentially (cache-friendly)
 * - Result C: written with column stride (cache-unfriendly)
 *
 * Performance impact: Expect significantly slower than Strategy 1 due to
 * poor cache utilization. Demonstrates importance of data layout alignment
 * with access patterns.
 *
 * Thread safety: GUARANTEED
 * - Disjoint output element assignment (no overlap)
 * - No synchronization required
 *
 * @param cfg  Thread configuration specifying input/output matrices and work range
 */
static void strategy_column_by_column(const ThreadConfig& cfg) {
    const int total_elements = cfg.C->rows * cfg.C->cols;

    // Iterate through assigned range of linear indices (column-major interpretation)
    for (int idx = cfg.start_idx; idx < cfg.end_idx && idx < total_elements; ++idx) {
        // Convert linear index to 2D coordinates (column-major order)
        // Formula: idx = col x rows + row
        int col = idx / cfg.C->rows;
        int row = idx % cfg.C->rows;

        // Compute and store result element
        // Each thread processes different elements - no conflicts
        cfg.C->at(row, col) = compute_element(*cfg.A, *cfg.B, row, col, cfg.thread_id);
    }
}


/**
 * STRATEGY 3: Strided (K-th Element) Distribution
 *
 * Work division: Each thread takes every K-th element (where K = num_threads),
 * processing in row-major order with stride equal to thread count.
 *
 * Example (9x9 result, 4 threads):
 * - Thread 0: indices 0, 4, 8, 12, 16, 20, 24, 28, 32, ... (elements 0, 4, 8, ...)
 * - Thread 1: indices 1, 5, 9, 13, 17, 21, 25, 29, 33, ... (elements 1, 5, 9, ...)
 * - Thread 2: indices 2, 6, 10, 14, 18, 22, 26, 30, 34, ... (elements 2, 6, 10, ...)
 * - Thread 3: indices 3, 7, 11, 15, 19, 23, 27, 31, 35, ... (elements 3, 7, 11, ...)
 *
 * Cache behavior: WORST
 * - Large stride access pattern maximizes cache misses
 * - Matrix A: irregular row access (thrashing cache lines)
 * - Matrix B: irregular column access (poor locality)
 * - Result C: strided writes (potential false sharing if stride < cache line)
 *
 * False sharing risk: When stride is small and cache line is large (typically 64 bytes),
 * multiple threads may write to different elements in the same cache line, causing
 * cache coherency traffic and severe performance degradation.
 *
 * Thread safety: GUARANTEED
 * - Disjoint indices ensure no write conflicts
 * - No explicit synchronization needed
 *
 * @param cfg  Thread configuration (start_idx represents thread_id, end_idx represents num_threads)
 */
static void strategy_kth_element(const ThreadConfig& cfg) {
    const int thread_id = cfg.start_idx;    // Repurpose: thread identifier
    const int num_threads = cfg.end_idx;     // Repurpose: total thread count
    const int total_elements = cfg.C->rows * cfg.C->cols;

    // Process every K-th element starting from thread_id
    // Stride of num_threads ensures no overlap between threads
    for (int idx = thread_id; idx < total_elements; idx += num_threads) {
        // Convert strided linear index to 2D coordinates (row-major)
        int row = idx / cfg.C->cols;
        int col = idx % cfg.C->cols;

        // Compute and store result element
        // Large stride minimizes cache reuse, demonstrating worst-case performance
        cfg.C->at(row, col) = compute_element(*cfg.A, *cfg.B, row, col, cfg.thread_id);
    }
}

/**
 * STRATEGY 4: Cache-Blocked with Optimized Thread Distribution
 *
 * Combines cache tiling with row-major thread distribution to achieve
 * both excellent cache behavior and optimal thread-level parallelism.
 *
 * Algorithm Overview:
 * 1. Each thread processes a contiguous range of rows from the result matrix
 * 2. Within its assigned rows, processes the matrix in cache-friendly blocks
 * 3. Blocks sized to fit in L1 cache (64x64 = ~16KB per block)
 * 4. Accumulates partial results across K-dimension blocks
 *
 * Performance characteristics:
 * - Reduces cache misses by factor of BLOCK_SIZE through data reuse
 * - Each matrix element loaded once and reused BLOCK_SIZE times
 * - Memory bandwidth reduced from O(n³) to O(n³/BLOCK_SIZE)
 *
 * @param cfg  Thread configuration with element range to process
 */
static void strategy_blocked_optimized(const ThreadConfig& cfg) {
    // Block size tuned for L1 cache: 64x64x4 bytes = 16KB per block
    // Three blocks (A, B, C) = 48KB total, fits comfortably in typical 32-64KB L1
    const int BLOCK_SIZE = 64;

    const int C_rows = cfg.C->rows;
    const int C_cols = cfg.C->cols;
    const int A_cols = cfg.A->cols;

    // THREAD WORK DISTRIBUTION
    // Convert linear element range [start_idx, end_idx) to row range
    // Example: 100x100 matrix, 4 threads
    // - Total elements: 10,000
    // - Thread 0: elements [0, 2500)     → rows [0, 25)
    // - Thread 1: elements [2500, 5000)  → rows [25, 50)
    // - Thread 2: elements [5000, 7500)  → rows [50, 75)
    // - Thread 3: elements [7500, 10000) → rows [75, 100)

    // Calculate starting row from starting element index
    // Integer division: element index / columns per row = row number
    int row_start = cfg.start_idx / C_cols;

    // Calculate ending row from ending element index
    // Round up: (end_idx - 1) / C_cols gives last row, +1 for exclusive upper bound
    int row_end = (cfg.end_idx + C_cols - 1) / C_cols;

    // Clamp to matrix bounds (safety check for last thread)
    row_end = std::min(row_end, C_rows);

    // BLOCKED MATRIX MULTIPLICATION
    // Standard formula: C[i][j] = sum(A[i][k] * B[k][j]) for k=0 to n-1
    // Blocked formula: Break into blocks and compute partial sums

    // Outer loop: Iterate through rows assigned to this thread
    for (int i = row_start; i < row_end; ++i) {

        // COLUMN BLOCKING (J dimension)
        // Process result matrix C in vertical strips of width BLOCK_SIZE
        // This groups nearby output elements for better cache locality

        for (int jj = 0; jj < C_cols; jj += BLOCK_SIZE) {
            // Calculate block boundary (handle edge case where cols not divisible by BLOCK_SIZE)
            int j_end = std::min(jj + BLOCK_SIZE, C_cols);

            // K DIMENSION BLOCKING (inner product accumulation)
            // Split the dot product computation into chunks of BLOCK_SIZE
            // Each iteration computes a partial sum using a block of A and B

            for (int kk = 0; kk < A_cols; kk += BLOCK_SIZE) {
                // Calculate block boundary for K dimension
                int k_end = std::min(kk + BLOCK_SIZE, A_cols);

                // INNER COMPUTATION (actual multiplication within block)
                // Now we compute one 64x64 block of the result
                // All data accessed here should fit in L1 cache

                // Iterate through columns in current J-block
                for (int j = jj; j < j_end; ++j) {
                    // Initialize or accumulate:
                    // - First K-block (kk=0): Start fresh with 0
                    // - Later K-blocks (kk>0): Add to existing partial sum
                    int sum = (kk == 0) ? 0 : cfg.C->at(i, j);

                    // Compute partial dot product for this K-block
                    // This is the core computation: multiply and accumulate
                    for (int k = kk; k < k_end; ++k) {
                        // A[i][k]: Reused for all j in this block (64 times)
                        // B[k][j]: Sequential access within block
                        sum += cfg.A->at(i, k) * cfg.B->at(k, j);
                    }

                    // Store partial sum back to result matrix
                    // On last K-block, this is the final result
                    cfg.C->at(i, j) = sum;
                }
            }
        }
    }
}


///////////////////////////
///  BENCHMARK SECTION  ///
///////////////////////////

/**
 * Performance measurement: Executes matrix multiplication with specified strategy.
 *
 * Spawns worker threads according to configuration, measures wall-clock time,
 * and reports throughput metrics. No synchronization overhead since work
 * partitioning is embarrassingly parallel.
 *
 * @param A               Left operand matrix
 * @param B               Right operand matrix
 * @param C               Result matrix (modified in place)
 * @param num_threads     Number of parallel worker threads
 * @param strategy        Function pointer to distribution strategy
 * @param strategy_name   Human-readable strategy identifier for output
 * @return                Execution time in milliseconds
 */
static double measure_performance(
        const Matrix& A,
        const Matrix& B,
        Matrix& C,
        int num_threads,
        void (*strategy)(const ThreadConfig&),
        const char* strategy_name
) {
    std::cout << "\n========================================\n";
    std::cout << "Strategy: " << strategy_name << "\n";
    std::cout << "========================================\n";
    std::cout << "Matrix dimensions: " << A.rows << "x" << A.cols
              << " x " << B.rows << "x" << B.cols << "\n";
    std::cout << "Result dimensions: " << C.rows << "x" << C.cols << "\n";
    std::cout << "Worker threads: " << num_threads << "\n";

    // Record start time with high-resolution clock
    auto start_time = std::chrono::high_resolution_clock::now();

    // Thread storage: holds worker thread objects
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const int total_elements = C.rows * C.cols;

    // Work distribution: divide elements as evenly as possible among threads
    // Strategy 3 requires different parameterization (handled in branch)
    if (strategy == strategy_kth_element) {
        // Strided strategy: each thread gets its own ID and total count
        for (int i = 0; i < num_threads; ++i) {
            ThreadConfig cfg{
                    .thread_id = i,
                    .A = &A,
                    .B = &B,
                    .C = &C,
                    .start_idx = i,              // Repurposed: thread identifier
                    .end_idx = num_threads       // Repurposed: stride value
            };
            threads.emplace_back(strategy, cfg);
        }
    } else {
        // Sequential strategies: divide total elements into contiguous ranges
        int elements_per_thread = total_elements / num_threads;
        int remainder = total_elements % num_threads;

        int current_start = 0;
        for (int i = 0; i < num_threads; ++i) {
            // Distribute remainder elements: first 'remainder' threads get +1 element
            int current_count = elements_per_thread + (i < remainder ? 1 : 0);
            int current_end = current_start + current_count;

            ThreadConfig cfg{
                    .thread_id = i,
                    .A = &A,
                    .B = &B,
                    .C = &C,
                    .start_idx = current_start,
                    .end_idx = current_end
            };
            threads.emplace_back(strategy, cfg);

            current_start = current_end;
        }
    }

    // Wait for all threads to complete computation
    // No explicit synchronization needed during computation - only join barrier
    for (auto& thread : threads) {
        thread.join();
    }

    // Record end time and compute duration
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double ms = duration.count() / 1000.0;

    // Report performance metrics
    std::cout << "Execution time: " << std::fixed << std::setprecision(3)
              << ms << " ms\n";
    std::cout << "Total operations: " << total_elements << "\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2)
              << (total_elements * 1000.0 / ms) << " elements/sec\n";

    return ms;
}


/**
 * Baseline measurement: Single-threaded matrix multiplication for comparison.
 *
 * Provides reference performance to evaluate parallel speedup and efficiency.
 *
 * @param A  Left operand matrix
 * @param B  Right operand matrix
 * @param C  Result matrix (modified in place)
 * @return   Execution time in milliseconds
 */
static double measure_baseline(const Matrix& A, const Matrix& B, Matrix& C) {
    std::cout << "\n========================================\n";
    std::cout << "Baseline: Single-threaded\n";
    std::cout << "========================================\n";
    std::cout << "Matrix dimensions: " << A.rows << "x" << A.cols
              << " x " << B.rows << "x" << B.cols << "\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    // Naive triple-loop matrix multiplication
    // Outer two loops iterate through result positions
    // Inner loop computes dot product for each element
    for (int i = 0; i < C.rows; ++i) {
        for (int j = 0; j < C.cols; ++j) {
            int sum = 0;
            for (int k = 0; k < A.cols; ++k) {
                sum += A.at(i, k) * B.at(k, j);
            }
            C.at(i, j) = sum;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double ms = duration.count() / 1000.0;

    std::cout << "Execution time: " << std::fixed << std::setprecision(3)
              << ms << " ms\n";

    return ms;
}


/**
 * Comprehensive experiment suite: Tests multiple configurations.
 *
 * Systematically varies matrix size and thread count to characterize
 * performance scaling behavior and identify optimal configurations.
 */
static void run_experiments() {
    // Test matrix sizes: spans small to large scale
    std::vector<int> matrix_sizes = {50, 100, 200, 500, 1000};

    // Test thread counts: evaluates scaling from serial to highly parallel
    std::vector<int> thread_counts = {1, 4, 16, 32};

    std::cout << "\n############################################\n";
    std::cout << "#  MATRIX MULTIPLICATION EXPERIMENTS      #\n";
    std::cout << "############################################\n";

    // Nested loops: test all combinations of size and thread count
    for (int size : matrix_sizes) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "Matrix size: " << size << "x" << size << "\n";
        std::cout << std::string(70, '=') << "\n";

        // Create and initialize test matrices
        Matrix A(size, size);
        Matrix B(size, size);
        A.randomize(1, 10);
        B.randomize(1, 10);

        // Baseline measurement for speedup calculation
        Matrix C_baseline(size, size);
        double baseline_time = measure_baseline(A, B, C_baseline);

        // Test each strategy with varying thread counts
        for (int num_threads : thread_counts) {
            // Skip configurations with more threads than elements (pathological case)
            if (num_threads > size * size) continue;

            // Strategy 1: Row-by-row distribution
            Matrix C1(size, size);
            double time1 = measure_performance(A, B, C1, num_threads,
                                               strategy_row_by_row,
                                               "Strategy 1: Row-by-Row");
            std::cout << "Speedup vs baseline: " << std::fixed << std::setprecision(2)
                      << (baseline_time / time1) << "x\n";

            // Strategy 2: Column-by-column distribution
            Matrix C2(size, size);
            double time2 = measure_performance(A, B, C2, num_threads,
                                               strategy_column_by_column,
                                               "Strategy 2: Column-by-Column");
            std::cout << "Speedup vs baseline: " << (baseline_time / time2) << "x\n";

            // Strategy 3: Strided (k-th element) distribution
            Matrix C3(size, size);
            double time3 = measure_performance(A, B, C3, num_threads,
                                               strategy_kth_element,
                                               "Strategy 3: Every k-th Element");
            std::cout << "Speedup vs baseline: " << (baseline_time / time3) << "x\n";

            // Strategy 4: Cache-Blocked with Optimized Thread Distribution
            Matrix C4(size, size);
            double time4 = measure_performance(A, B, C3, num_threads,
                                               strategy_blocked_optimized,
                                               "Strategy 4: Cache-Blocked with Optimized Thread Distribution");
            std::cout << "Speedup vs baseline: " << (baseline_time / time3) << "x\n";
        }
    }
}


/////////////////////////
///   MAIN SECTION    ///
/////////////////////////

/**
 * Main entry point: Orchestrates matrix multiplication benchmarks.
 *
 * Demonstrates embarrassingly parallel computation where no synchronization
 * primitives (mutexes, atomics) are required. Each thread operates on
 * completely independent output regions, showcasing ideal parallel scaling.
 *
 * Command-line modes:
 * - (no args): Quick demonstration with small matrix
 * - --full: Comprehensive benchmark suite
 * - --debug: Small matrix with verbose element-level output
 */
int main(int argc, char* argv[]) {
    // Debug Flag
    bool DEBUG = false;

    // Seed random number generator for reproducible test data
    srand(static_cast<unsigned>(time(nullptr)));

    std::cout << "############################################\n";
    std::cout << "#  MATRIX MULTIPLICATION PARALLEL TEST   #\n";
    std::cout << "############################################\n\n";

    // Display system information
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency()
              << " threads\n";
    std::cout << "Pointer size: " << (sizeof(void*) * 8) << "-bit\n";

    // Mode selection based on command-line arguments
    if (DEBUG == false) {
        // Full experiment suite: multiple sizes and thread counts
        run_experiments();

    } else {
        // Debug mode: small matrix with detailed element-level logging
        std::cout << "\n############################################\n";
        std::cout << "#  DEBUG MODE: Element-level output       #\n";
        std::cout << "############################################\n";

        int size = 9;
        int num_threads = 4;

        Matrix A(size, size);
        Matrix B(size, size);
        Matrix C(size, size);

        A.randomize(1, 5);
        B.randomize(1, 5);

        std::cout << "\nMatrix A:\n";
        A.print();
        std::cout << "\nMatrix B:\n";
        B.print();

        // Run one strategy with debug output enabled (shows thread assignments)
        measure_performance(A, B, C, num_threads, strategy_kth_element,
                            "Strategy 1: Row-by-Row (Debug Mode)");

        std::cout << "\nResult Matrix C:\n";
        C.print();
    }

    std::cout << "\n############################################\n";
    std::cout << "#  ALL TESTS COMPLETED                    #\n";
    std::cout << "############################################\n";

    return 0;
}
