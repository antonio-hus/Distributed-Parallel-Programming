///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>


///////////////////////////
///   TYPEDEFS SECTION  ///
///////////////////////////
using Graph = std::vector<std::vector<int>>; // Adjacency list
using Path = std::vector<int>;


///////////////////////////
///   SOLVER SECTION    ///
///////////////////////////

/**
 * Class responsible for parallel exploration to find a Hamiltonian cycle
 * using a user-specified number of threads. Thread allocation is performed
 * at each search tree branch by splitting available threads across the
 * neighbor sets to maximize parallelism.
 */
class HamiltonianSolver
{
public:
    const Graph graph;             // Input directed graph
    const int N;                   // Number of vertices in the graph
    std::atomic<bool> found;       // Indicates if a solution has been found
    std::mutex solution_mtx;       // Guards access to the solution path
    Path solution;                 // Stores the solution cycle if found

    /**
     * Construct the solver with the input graph.
     * @param g Graph represented as adjacency list.
     */
    explicit HamiltonianSolver(Graph g): graph(std::move(g)), N(graph.size()), found(false){}

    /** Entry point for solving with a specified thread count.
     * @param nrThreads Number of threads for search parallelization
     * @param startVertex Which vertex to start the cycle from (typically 0)
     */
    void solve(unsigned nrThreads, int startVertex = 0)
    {
        Path visited;  visited.push_back(startVertex);
        std::vector<bool> used(N, false);
        used[startVertex] = true;
        search(visited, used, startVertex, nrThreads);
    }

    /**
     * Recursive parallel backtracking search.
     *
     * At each node, the available threads are split between the
     * unexplored neighbors for max concurrency.
     *
     * @param path       Current path being explored
     * @param used       Boolean vector marking used nodes
     * @param current    Current node being visited
     * @param threads    Number of threads allocated to this subtree
     */
    void search(Path path, std::vector<bool> used, int current, unsigned threads)
    {
        if (found) return;

        // Check if all nodes are visited and can close a cycle
        if (path.size() == N) {
            for (int neighbor : graph[current]) {
                if (neighbor == path.front()) { // Can close the cycle
                    bool wasSet = !found.exchange(true);
                    if (wasSet) {
                        std::lock_guard<std::mutex> lk(solution_mtx);
                        solution = path;
                        solution.push_back(path.front());
                    }
                    return;
                }
            }
            return;
        }

        // Get all unused neighbors
        std::vector<int> nexts;
        for (int neighbor : graph[current]) {
            if (!used[neighbor]) nexts.push_back(neighbor);
        }
        if (nexts.empty()) return; // Dead end

        // Number of subtasks to spawn equals number of neighbors (up to thread limit)
        unsigned subtasks = std::min<unsigned>(threads, nexts.size());

        if (threads > 1 && subtasks > 1) {
            // Allocate threads among neighbor branches
            std::vector<std::future<void>> futures;
            size_t consumed = 0;
            for (unsigned i = 0; i < subtasks; ++i) {
                // Divide neighbors among subtasks
                size_t left = nexts.size() - consumed;
                size_t alloc = left / (subtasks - i);
                if (left % (subtasks - i)) ++alloc;
                std::vector<int> thisChunk(nexts.begin() + consumed,
                                           nexts.begin() + consumed + alloc);
                consumed += alloc;

                futures.emplace_back(std::async(std::launch::async,[this, thisChunk, threads, subtasks, path, used]() {
                    for (int neighbor : thisChunk) {
                        if (this->found) return;
                        auto pathCopy = path;
                        pathCopy.push_back(neighbor);
                        auto usedCopy = used;
                        usedCopy[neighbor] = true;
                        this->search(std::move(pathCopy), std::move(usedCopy), neighbor, threads / subtasks);
                    }
                }));
            }
            for (auto& future : futures) future.wait();
        } else {
            // Sequential search when only one thread is available
            for (int neighbor : nexts) {
                if (found) return;
                path.push_back(neighbor);
                used[neighbor] = true;
                search(path, used, neighbor, 1);
                used[neighbor] = false;
                path.pop_back();
            }
        }
    }
};

///////////////////////////
///   MAIN SECTION      ///
///////////////////////////

/**
 * Generates a test graph and attempts to find a Hamiltonian cycle.
 */
int main()
{
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    struct TestGraph {
        std::string name;
        Graph graph;
    };

    // Define several graphs (some have Hamiltonian cycles, some don't)
    std::vector<TestGraph> tests{
            {
                    "Directed 5-cycle",
                    {
                            {1,2},  // 0
                            {2,3},  // 1
                            {3,4},  // 2
                            {4,0},  // 3
                            {0,1}   // 4
                    }
            },
            {
                    "Complete graph with 6 vertices",
                    {
                            {1,2,3,4,5}, // 0
                            {0,2,3,4,5}, // 1
                            {0,1,3,4,5}, // 2
                            {0,1,2,4,5}, // 3
                            {0,1,2,3,5}, // 4
                            {0,1,2,3,4}  // 5
                    }
            },
            {
                    "Line graph (no cycle)",
                    {
                            {1},   // 0
                            {2},   // 1
                            {3},   // 2
                            {4},   // 3
                            {}     // 4
                    }
            },
            {
                    "Wheel graph with 7 vertices",
                    {
                            {1,2,3,4,5,6}, // 0 (hub)
                            {0,2},     // 1
                            {0,3},     // 2
                            {0,4},     // 3
                            {0,5},     // 4
                            {0,6},     // 5
                            {0,1}      // 6
                    }
            },
            {
                    "Sparse graph (Hamiltonian path but no cycle)",
                    {
                            {1}, // 0
                            {2}, // 1
                            {3}, // 2
                            {4}, // 3
                            {0}  // 4
                    }
            }
    };

    for (const auto& test : tests) {
        std::cout << "\n===== Test: " << test.name << " =====\n";
        auto start = std::chrono::high_resolution_clock::now();
        HamiltonianSolver solver(test.graph);
        solver.solve(numThreads, 0);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (solver.found) {
            std::lock_guard<std::mutex> lk(solver.solution_mtx);
            std::cout << "Hamiltonian cycle found: ";
            for (int v : solver.solution) std::cout << v << " ";
            std::cout << "\n";
        } else {
            std::cout << "No Hamiltonian cycle found.\n";
        }
        std::cout << "Execution time: " << ms << " ms\n";
    }
    return 0;
}

