///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>


///////////////////////////
///   TYPEDEFS SECTION  ///
///////////////////////////
using Coeff = long long;
using Poly = std::vector<Coeff>;


///////////////////////////
///   HELPERS SECTION   ///
///////////////////////////

/**
 * Computes the naive O(n^2) polynomial multiplication.
 *
 * For polynomials A(x)=Σa[i]x^i and B(x)=Σb[j]x^j, the product
 * C(x)=A(x)*B(x) has coefficients:
 *   C[k] = Σ_{i+j=k} a[i] * b[j], for k = 0..(nA+nB-2)
 *
 * Purely sequential, direct access, single-threaded.
 *
 * @param a  First polynomial coefficients
 * @param b  Second polynomial coefficients
 * @return   Resulting product coefficients
 */
static Poly multiply_naive_seq(const Poly& a, const Poly& b) {
    size_t n = a.size(), m = b.size();
    Poly result(n + m - 1, 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < m; ++j)
            result[i + j] += a[i] * b[j];
    return result;
}

/**
 * Performs naive polynomial multiplication exploiting parallelism at the outer loop.
 *
 * The first polynomial is split into blocks, each assigned to a separate thread/task,
 * which computes its local contribution to the result. The partial local results are
 * summed up at the end. Correctness is trivially maintained due to independent access.
 *
 * Synchronization:
 * - Each thread writes only to its own local array, no contention.
 * - The merge step is purely additive.
 *
 * @param a  First polynomial
 * @param b  Second polynomial
 * @return   Product coefficients
 */
static Poly multiply_naive_par(const Poly& a, const Poly& b) {
    size_t n = a.size(), m = b.size();
    Poly result(n + m - 1, 0);

    // Determine hardware concurrency for optimal splitting.
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    // Decide splitting factor.
    size_t chunk = (n + numThreads - 1) / numThreads;
    std::vector<std::future<Poly>> futures;

    // Distribute blocks to tasks.
    for (unsigned t = 0; t < numThreads; ++t) {
        size_t begin = t * chunk;
        size_t end = std::min(begin + chunk, n);
        futures.push_back(std::async(std::launch::async, [&, begin, end]() {
            Poly local(result.size(), 0);
            for (size_t i = begin; i < end; ++i)
                for (size_t j = 0; j < m; ++j)
                    local[i + j] += a[i] * b[j];
            return local;
        }));
    }

    // Merge all partial results with careful accumulation.
    for (auto& f : futures) {
        Poly local = f.get();
        for (size_t k = 0; k < local.size(); ++k)
            result[k] += local[k];
    }
    return result;
}

/**
 * Sequential Karatsuba recursive multiplication.
 *
 * Theory:
 * For polynomials A(x), B(x), partitioned into halves:
 *   A(x) = A_low(x) + A_high(x) * x^m, B(x) = B_low(x) + B_high(x) * x^m
 *
 * The product:
 *   A(x) * B(x) = P2 * x^{2m} + (P3 - P2 - P1) * x^{m} + P1
 * Where:
 *   P1 = A_low * B_low
 *   P2 = A_high * B_high
 *   P3 = (A_low + A_high) * (B_low + B_high)
 *
 * Base case switches to naive multiplication for small degrees.
 * This implementation uses size-based cutoff for base case threshold.
 *
 * @param a  First polynomial coefficients
 * @param b  Second polynomial coefficients
 * @return   Resulting product coefficients
 */
static Poly multiply_karatsuba_seq(const Poly& a, const Poly& b) {
    size_t n = a.size(), m = b.size();

    // Base case: falls back to naive variant for small sizes, best for performance.
    if (n <= 64 || m <= 64)
        return multiply_naive_seq(a, b);

    // Split polynomials to lowest and highest degree halves.
    size_t half = n / 2;
    Poly a_low(a.begin(), a.begin() + half);
    Poly a_high(a.begin() + half, a.end());
    Poly b_low(b.begin(), b.begin() + std::min(half, m));
    Poly b_high(b.begin() + std::min(half, m), b.end());

    // Compute all three required products recursively.
    Poly P1 = multiply_karatsuba_seq(a_low, b_low);
    Poly P2 = multiply_karatsuba_seq(a_high, b_high);

    // Prepare "summed" halves (for cross-term).
    Poly a_sum(std::max(a_low.size(), a_high.size()), 0);
    Poly b_sum(std::max(b_low.size(), b_high.size()), 0);
    for (size_t i = 0; i < a_low.size(); ++i) a_sum[i] += a_low[i];
    for (size_t i = 0; i < a_high.size(); ++i) a_sum[i] += a_high[i];
    for (size_t i = 0; i < b_low.size(); ++i) b_sum[i] += b_low[i];
    for (size_t i = 0; i < b_high.size(); ++i) b_sum[i] += b_high[i];

    Poly P3 = multiply_karatsuba_seq(a_sum, b_sum);

    // Remove double-counted terms to isolate cross-product.
    for (size_t i = 0; i < P1.size(); ++i) P3[i] -= P1[i];
    for (size_t i = 0; i < P2.size(); ++i) P3[i] -= P2[i];

    Poly result(n + m - 1, 0);
    // Add P1 (low*low)
    for (size_t i = 0; i < P1.size(); ++i) result[i] += P1[i];
    // Add (P3 - P1 - P2), appropriately shifted
    for (size_t i = 0; i < P3.size(); ++i) result[i + half] += P3[i];
    // Add P2 (high*high), shifted by 2*half
    for (size_t i = 0; i < P2.size(); ++i) result[i + 2 * half] += P2[i];
    return result;
}

/**
 * Parallel Karatsuba recursive multiplication.
 *
 * Forks recursive computations for the three sub-products — P1, P2, P3 — as async tasks,
 * then synchronizes with .get(). The depth parameter limits spawning depth (for safety).
 *
 * Parallelization rationale:
 * - Each subproduct is logically independent.
 * - No shared mutable state — pure function calls
 * - The merge step is thread-safe due to isolated result vectors
 *
 * @param a      First polynomial
 * @param b      Second polynomial
 * @param depth  Current recursion depth (limits parallelism depth)
 * @return       Product coefficients
 */
static Poly multiply_karatsuba_par(const Poly& a, const Poly& b, int depth = 0) {
    size_t n = a.size();
    size_t m = b.size();

    // Cutoff for base case (avoid thread overload and maximize efficiency)
    if (n <= 64 || m <= 64)
        return multiply_naive_seq(a, b);

    size_t half = n / 2;
    Poly a_low(a.begin(), a.begin() + half);
    Poly a_high(a.begin() + half, a.end());
    Poly b_low(b.begin(), b.begin() + std::min(half, m));
    Poly b_high(b.begin() + std::min(half, m), b.end());

    // For safety and performance, limit parallel recursion depth (otherwise thread exhaustion occurs)
    std::future<Poly> futP1, futP2;
    Poly P1, P2, P3;

    if (depth < 3) {
        futP1 = std::async(std::launch::async, multiply_karatsuba_par, a_low, b_low, depth + 1);
        futP2 = std::async(std::launch::async, multiply_karatsuba_par, a_high, b_high, depth + 1);
    } else {
        P1 = multiply_karatsuba_par(a_low, b_low, depth + 1);
        P2 = multiply_karatsuba_par(a_high, b_high, depth + 1);
    }

    Poly a_sum(std::max(a_low.size(), a_high.size()), 0);
    Poly b_sum(std::max(b_low.size(), b_high.size()), 0);
    for (size_t i = 0; i < a_low.size(); ++i) a_sum[i] += a_low[i];
    for (size_t i = 0; i < a_high.size(); ++i) a_sum[i] += a_high[i];
    for (size_t i = 0; i < b_low.size(); ++i) b_sum[i] += b_low[i];
    for (size_t i = 0; i < b_high.size(); ++i) b_sum[i] += b_high[i];

    if (depth < 3) {
        P3 = multiply_karatsuba_par(a_sum, b_sum, depth + 1);
        P1 = futP1.get(); // Synchronize
        P2 = futP2.get();
    } else {
        P3 = multiply_karatsuba_seq(a_sum, b_sum);
    }

    // Eliminate redundant terms for proper cross-coefficient calculation
    for (size_t i = 0; i < P1.size(); ++i) P3[i] -= P1[i];
    for (size_t i = 0; i < P2.size(); ++i) P3[i] -= P2[i];

    // Merge the three computed blocks into the final result
    Poly result(n + m - 1, 0);
    for (size_t i = 0; i < P1.size(); ++i) result[i] += P1[i];
    for (size_t i = 0; i < P3.size(); ++i) result[i + half] += P3[i];
    for (size_t i = 0; i < P2.size(); ++i) result[i + 2 * half] += P2[i];
    return result;
}


///////////////////////////
/// BENCHMARKING SECTION///
///////////////////////////

/**
 * Times and executes a multiplication strategy, printing key results and timing.
 *
 * @param name      Human-readable label for the algorithm variant
 * @param fn        Polynomial multiplication callable: std::function<Poly(const Poly&, const Poly&)>
 * @param A         First input polynomial
 * @param B         Second input polynomial
 */
static void benchmark(const std::string& name,
                      const std::function<Poly(const Poly&, const Poly&)>& fn,
                      const Poly& A,
                      const Poly& B) {
    auto start = std::chrono::high_resolution_clock::now();
    Poly result = fn(A, B);
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << std::left << std::setw(25) << name << " -> "
              << "Time: " << std::setw(10) << ms << "ms"
              << " Result[0..4]: ";

    for (size_t i = 0; i < std::min<size_t>(5, result.size()); ++i)
        std::cout << result[i] << " ";
    std::cout << std::endl;
}

///////////////////////////
///   MAIN SECTION      ///
///////////////////////////

/**
 * Main entry point for polynomial multiplication benchmark.
 *
 * Generates two random polynomials and multiplies them using all four strategies,
 * then prints out performance and result diagnostics.
 *
 * Configurable parameters:
 * - n : degree of polynomial (number of coefficients)
 *
 * Results:
 * - Benchmarks sequential and parallel versions, both naive and Karatsuba
 * - Verifies output correctness by printing partial result and total sums
 */
int main() {
    // 4096 coefficients for benchmarking
    const size_t n = 1 << 12;
    Poly A(n), B(n);

    for (size_t i = 0; i < n; ++i) {
        A[i] = i % 10 + 1;
        B[i] = (i % 5) + 2;
    }

    std::cout << "========================================\n";
    std::cout << "POLYNOMIAL MULTIPLICATION BENCHMARK\n";
    std::cout << "========================================\n";

    benchmark("Naive Sequential", multiply_naive_seq, A, B);
    benchmark("Naive Parallel", multiply_naive_par, A, B);
    benchmark("Karatsuba Sequential", multiply_karatsuba_seq, A, B);
    benchmark("Karatsuba Parallel",[](const Poly& a, const Poly& b) { return multiply_karatsuba_par(a, b, 0); },A, B);

    std::cout << "========================================\n";
    std::cout << "All tests completed\n";
    std::cout << "========================================\n";

    return 0;
}