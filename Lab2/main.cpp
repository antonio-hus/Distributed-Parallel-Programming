///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <random>
#include <deque>
#include <chrono>


///////////////////////////
///   STRUCTS SECTION   ///
///////////////////////////
/**
 * SharedData: Communication channel between producer and consumer threads.
 *
 * Uses a bounded deque to buffer products between producer and consumer.
 * The deque size affects performance - larger deques reduce synchronization
 * overhead but increase memory usage.
 */
struct SharedData {
    // Mutex protecting concurrent access to all shared variables.
    std::mutex mtx;

    // Condition variable for producer: signals when deque has space
    std::condition_variable cv_producer;

    // Condition variable for consumer: signals when deque has data
    std::condition_variable cv_consumer;

    // Bounded deque storing computed products
    std::deque<double> product_deque;

    // Maximum deque size (configurable buffer size)
    size_t max_deque_size;

    // Flag indicating all products have been computed
    bool done;

    /**
     * Constructor: initializes shared state with specified deque size.
     * @param deque_size Maximum number of products that can be buffered
     */
    SharedData(size_t deque_size): max_deque_size(deque_size), done(false) {}
};


///////////////////////////
///   THREAD FUNCTIONS  ///
///////////////////////////
/**
 * Producer thread: computes products and adds them to the deque.
 *
 * Synchronization protocol:
 * 1. Compute product locally (no lock needed)
 * 2. Acquire lock and wait until deque has space
 * 3. Push product to deque
 * 4. Signal consumer via condition variable
 * 5. After all products, set done flag and signal
 */
void producer(const std::vector<double>& v1, const std::vector<double>& v2, SharedData& data) {
    size_t n = v1.size();

    for (size_t i = 0; i < n; i++) {
        // Compute product locally without holding any locks
        double local_product = v1[i] * v2[i];

        {
            std::unique_lock<std::mutex> lck(data.mtx);

            // Wait until deque has space (not full)
            while (data.product_deque.size() >= data.max_deque_size) {
                data.cv_producer.wait(lck);
            }

            // Add product to deque
            data.product_deque.push_back(local_product);

//            std::cout << "Producer: v1[" << i << "] * v2[" << i
//                      << "] = " << local_product
//                      << " (deque size: " << data.product_deque.size() << ")" << std::endl;
        }

        // Notify consumer that new data is available
        data.cv_consumer.notify_one();
    }

    // Signal completion
    {
        std::unique_lock<std::mutex> lck(data.mtx);
        data.done = true;
    }
    data.cv_consumer.notify_one();
}

/**
 * Consumer thread: consumes products from deque and accumulates sum.
 *
 * Synchronization protocol:
 * 1. Acquire lock and wait until deque has data or producer is done
 * 2. Pop product from deque
 * 3. Signal producer that space is available
 * 4. Add product to sum outside critical section
 * 5. Repeat until deque empty and producer done
 */
void consumer(double& result, SharedData& data) {
    result = 0.0;
    int count = 0;

    while (true) {
        double local_product;
        bool got_product = false;

        {
            std::unique_lock<std::mutex> lck(data.mtx);

            // Wait until deque has data OR producer is done
            while (data.product_deque.empty() && !data.done) {
                data.cv_consumer.wait(lck);
            }

            // Check if we should terminate
            if (data.product_deque.empty() && data.done) {
                break;
            }

            // Get product from deque
            if (!data.product_deque.empty()) {
                local_product = data.product_deque.front();
                data.product_deque.pop_front();
                got_product = true;

//                std::cout << "Consumer: consumed product #" << count
//                          << " = " << local_product
//                          << " (deque size: " << data.product_deque.size() << ")" << std::endl;
            }
        }

        // Notify producer that space is available
        if (got_product) {
            data.cv_producer.notify_one();
            result += local_product;
            count++;
        }
    }

//    std::cout << "Consumer: finished, scalar product = " << result << std::endl;
}


/////////////////////////
///   MAIN SECTION    ///
/////////////////////////
/**
 * Runs a single experiment with specified deque size.
 * Returns execution time in milliseconds.
 */
double run_experiment(int n, size_t deque_size, bool verbose) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 10.0);

    std::vector<double> v1(n);
    std::vector<double> v2(n);

    for (int i = 0; i < n; i++) {
        v1[i] = dist(gen);
        v2[i] = dist(gen);
    }

    double scalar_product = 0.0;
    SharedData shared_data(deque_size);

    if (verbose) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "EXPERIMENT: Deque Size = " << deque_size << std::endl;
        std::cout << "========================================" << std::endl;
    }

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer_thread(producer, std::ref(v1), std::ref(v2), std::ref(shared_data));
    std::thread consumer_thread(consumer, std::ref(scalar_product), std::ref(shared_data));

    producer_thread.join();
    consumer_thread.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (verbose) {
        std::cout << "\nResult: " << scalar_product << std::endl;
        std::cout << "Time: " << duration.count() << " ms" << std::endl;
    }

    return duration.count();
}

int main() {
    const int n = 10000;  // Vector size
    std::vector<size_t> deque_sizes = {1, 2, 5, 10, 50, 100, 500, 1000};

    std::cout << "========================================" << std::endl;
    std::cout << "SCALAR PRODUCT - DEQUE SIZE ANALYSIS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Vector size: " << n << " elements" << std::endl;
    std::cout << "Testing deque sizes: ";
    for (size_t sz : deque_sizes) {
        std::cout << sz << " ";
    }
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;

    // Run experiments with different deque sizes
    std::cout << "\nRUNNING PERFORMANCE EXPERIMENTS...\n" << std::endl;

    for (size_t deque_size : deque_sizes) {
        // Run multiple times and average (for more reliable results)
        const int runs = 3;
        double total_time = 0.0;

        for (int i = 0; i < runs; i++) {
            double time = run_experiment(n, deque_size, false);
            total_time += time;
        }

        double avg_time = total_time / runs;
        std::cout << "Deque size " << deque_size << ": "
                  << avg_time << " ms (avg of " << runs << " runs)" << std::endl;
    }

//    std::cout << "\n========================================" << std::endl;
//    std::cout << "DETAILED RUN (Deque Size = 10)" << std::endl;
//    std::cout << "========================================" << std::endl;
//    run_experiment(100, 10, true);  // Smaller vector for detailed output

    return 0;
}