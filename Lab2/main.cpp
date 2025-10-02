///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <random>


///////////////////////////
///   STRUCTS SECTION   ///
///////////////////////////
/**
 * SharedData: Communication channel between producer and consumer threads.
 *
 * Encapsulates all shared state required for producer-consumer synchronization.
 * The producer computes products of vector elements and feeds them one at a time
 * to the consumer, which accumulates the sum.
 */
struct SharedData {
    // Mutex protecting concurrent access to all shared variables.
    // Must be acquired before reading or modifying any field below.
    std::mutex mtx;

    // Condition variable for thread synchronization.
    // Producer signals when a new product is ready.
    // Consumer waits until signaled by producer.
    std::condition_variable cv;

    // Flag indicating whether a product is ready for consumption.
    // true: consumer may read current_product
    // false: consumer must wait for producer
    bool product_ready;

    // Flag indicating all products have been computed.
    // Producer sets this to true after computing the final product.
    // Consumer uses this to know when to stop waiting.
    bool done;

    // The current product value available for consumption.
    // Only valid when product_ready == true.
    // Contains the result of v1[i] * v2[i] for some index i.
    double current_product;

    /**
     * Constructor: initializes shared state to starting values.
     * No products ready initially, producer hasn't finished.
     */
    SharedData(): product_ready(false), done(false), current_product(0.0){}
};


///////////////////////////
///   THREAD FUNCTIONS  ///
///////////////////////////
/**
 * Producer thread: computes products of corresponding vector elements.
 *
 * For each index i, computes v1[i] * v2[i] and feeds it to the consumer.
 * Implements the producer side of the producer-consumer pattern with
 * condition variables for synchronization.
 *
 * Synchronization protocol:
 * 1. Compute product locally (no lock needed)
 * 2. Acquire lock and wait until previous product consumed
 * 3. Store product and set product_ready flag
 * 4. Signal consumer via condition variable
 * 5. After all products, set done flag and signal
 *
 * @param v1    First input vector (read-only)
 * @param v2    Second input vector (read-only, must have same size as v1)
 * @param data  Shared communication channel with consumer
 */
void producer(const std::vector<double>& v1,const std::vector<double>& v2,SharedData& data) {
    unsigned long long n = v1.size();

    // Iterate through all vector elements
    for (unsigned long long i = 0; i < n; i++) {
        // Compute product locally without holding any locks.
        // This minimizes the critical section duration and reduces contention.
        double local_product = v1[i] * v2[i];

        {
            // Enter critical section
            std::unique_lock<std::mutex> lck(data.mtx);

            // Wait until consumer has consumed the previous product.
            // If product_ready is still true, consumer hasn't processed it yet.
            // The while loop protects against spurious wakeups.
            while (data.product_ready) {
                // Wait releases the lock atomically and puts thread to sleep.
                // Upon waking, the lock is automatically re-acquired.
                data.cv.wait(lck);
            }

            // Store the computed product in shared memory.
            // At this point, product_ready is false (consumer consumed previous).
            data.current_product = local_product;
            data.product_ready = true;

            // Debug output showing producer activity
            std::cout << "Producer: v1[" << i << "] * v2[" << i
                      << "] = " << v1[i] << " * " << v2[i]
                      << " = " << local_product << std::endl;
        }
        // Lock released here (end of scope)

        // Notify the consumer that a new product is available.
        // notify_one() wakes up the consumer if it's waiting.
        // Safe to call outside the lock - more efficient.
        data.cv.notify_one();
    }

    // All products computed - signal completion to consumer
    {
        std::unique_lock<std::mutex> lck(data.mtx);
        data.done = true;
    }

    // Final notification: wake consumer so it can check done flag
    data.cv.notify_one();
}

/**
 * Consumer thread: sums products provided by the producer.
 *
 * Waits for each product to become available, then adds it to the running sum.
 * Implements the consumer side of the producer-consumer pattern.
 *
 * Synchronization protocol:
 * 1. Acquire lock and wait until product_ready or done
 * 2. If product available, consume it (copy to local variable)
 * 3. Set product_ready = false to signal producer
 * 4. Notify producer via condition variable
 * 5. Add product to sum outside critical section
 * 6. Repeat until all products consumed
 *
 * @param n      Expected number of products to consume
 * @param result Output parameter: stores the final scalar product
 * @param data   Shared communication channel with producer
 */
void consumer(int n, double& result, SharedData& data) {
    // Initialize accumulator
    result = 0.0;
    int count = 0;

    // Continue until all products have been consumed
    while (count < n) {
        double local_product;

        {
            // Enter critical section
            std::unique_lock<std::mutex> lck(data.mtx);

            // Wait until a product is ready OR producer signals completion.
            // The while loop is CRITICAL: protects against spurious wakeups.
            // Condition variables can wake up without notification (spurious wakeup),
            // so we must re-check the predicate after each wake.
            while (!data.product_ready && !data.done) {
                // Wait releases the lock atomically and puts thread to sleep.
                // This atomic release+sleep prevents the race condition where:
                // - Consumer checks product_ready (false)
                // - Producer sets product_ready=true and signals
                // - Consumer enters wait (misses the signal)
                // Instead, wait() atomically releases lock and sleeps, ensuring
                // producer cannot signal between check and sleep.
                data.cv.wait(lck);
            }

            // Check termination condition.
            // If done is true but no product ready, producer has finished.
            if (!data.product_ready && data.done) {
                break;
            }

            // Consume the product (copy to local variable).
            // Copying allows us to minimize time spent holding the lock.
            local_product = data.current_product;
            data.product_ready = false;  // Signal producer: slot is free

            // Optional: Debug output showing consumer activity
            std::cout << "Consumer: consumed product #" << count
                      << " = " << local_product << std::endl;
        }
        // Lock released here (end of scope)

        // Notify producer that we've consumed the product.
        // Producer may be waiting for product_ready to become false.
        // notify_one() is sufficient - only one producer thread exists.
        data.cv.notify_one();

        // Add to result OUTSIDE critical section.
        // No lock needed - result is local to consumer thread.
        // This reduces lock contention with producer.
        result += local_product;
        count++;
    }

    std::cout << "Consumer: finished, scalar product = " << result << std::endl;
}


/////////////////////////
///   MAIN SECTION    ///
/////////////////////////
/**
 * Main entry point: Computes scalar product using producer-consumer pattern.
 *
 * Creates two vectors with randomly generated values, spawns producer and
 * consumer threads, and synchronizes them using condition variables and mutex.
 * The producer computes individual products while the consumer accumulates
 * them into the final result.
 *
 * This demonstrates:
 * - Condition variable usage for thread synchronization
 * - Producer-consumer communication pattern
 * - Atomic lock release and sleep to prevent race conditions
 * - Proper handling of spurious wakeups (while loops around wait())
 */
int main() {
    // Seed source for non-deterministic randomness
    std::random_device rd;
    // Initialize Mersenne Twister with random seed
    std::mt19937 gen(rd());

    // Uniform distribution for generating random doubles in range [0.0, 10.0]
    std::uniform_real_distribution<double> dist(0.0, 10.0);

    // Generate large vectors with random values for performance testing
    // Vector size - increase for stress testing
    const int n = 1000;

    std::vector<double> v1(n);
    std::vector<double> v2(n);

    // Populate vectors with random values
    for (int i = 0; i < n; i++) {
        v1[i] = dist(gen);
        v2[i] = dist(gen);
    }

    // Variable to store the final scalar product result
    double scalar_product = 0.0;

    // Shared data structure for producer-consumer communication
    SharedData shared_data;

    std::cout << "========================================" << std::endl;
    std::cout << "SCALAR PRODUCT COMPUTATION" << std::endl;
    std::cout << "Producer-Consumer with Condition Variables" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Vector size: " << n << " elements" << std::endl;
    std::cout << "Value range: [0.0, 10.0]" << std::endl;
    std::cout << "Random seed: non-deterministic" << std::endl;

    // Display first 5 elements as sample
    std::cout << "\nSample values:" << std::endl;
    std::cout << "Vector 1: ";
    for (int i = 0; i < std::min(5, n); i++) {
        std::cout << v1[i] << " ";
    }
    std::cout << "..." << std::endl;

    std::cout << "Vector 2: ";
    for (int i = 0; i < std::min(5, n); i++) {
        std::cout << v2[i] << " ";
    }
    std::cout << "..." << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    // Create producer thread: computes products of corresponding elements.
    std::thread producer_thread(producer, std::ref(v1), std::ref(v2), std::ref(shared_data));

    // Create consumer thread: accumulates products into final result.
    // Must pass scalar_product by reference so modifications are visible in main.
    std::thread consumer_thread(consumer, n, std::ref(scalar_product), std::ref(shared_data));

    // Wait for both threads to complete execution.
    // join() blocks until the thread finishes.
    // Order doesn't matter - both threads will complete regardless.
    producer_thread.join();
    consumer_thread.join();

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "FINAL RESULT" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Scalar product: " << scalar_product << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
