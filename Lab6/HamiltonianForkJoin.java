///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.util.stream.*;

///////////////////////////
///   SOLVER SECTION    ///
///////////////////////////

/**
 * Parallel Hamiltonian Cycle solver using Java ForkJoinPool and RecursiveTask.
 * - Given a directed graph, searches for a Hamiltonian cycle starting from vertex 0.
 * - Parallelizes search by dividing neighbor branches among ForkJoin subtasks.
 * - Stops on first found solution (if any).
 */
public class HamiltonianForkJoin
{
    // Graph represented as adjacency list
    private final List<List<Integer>> graph;
    private final int N;
    private final AtomicBoolean found = new AtomicBoolean(false);
    private List<Integer> solution = null;
    private final Object lock = new Object();

    /**
     * Constructs a solver with the given directed graph.
     * @param graph adjacency list representation (List of neighbor Lists)
     */
    public HamiltonianForkJoin(List<List<Integer>> graph) {
        this.graph = graph;
        this.N = graph.size();
    }

    /**
     * Starts the parallel solve process using a specified thread pool.
     * @return the Hamiltonian cycle as a list of vertices (including return to start), or null if not found.
     */
    public List<Integer> solve(ForkJoinPool pool) {
        List<Integer> path = new ArrayList<>();
        path.add(0);
        boolean[] used = new boolean[N];
        used[0] = true;
        pool.invoke(new ExploreTask(path, used, 0));
        return solution;
    }

    /**
     * Recursive search task for Hamiltonian cycle using ForkJoin.
     * Each branch of the search tree is a subtask, utilizing available
     * parallelism in the ForkJoinPool.
     */
    private class ExploreTask extends RecursiveTask<Boolean> {
        private final List<Integer> path;
        private final boolean[] used;
        private final int current;

        public ExploreTask(List<Integer> path, boolean[] used, int current) {
            this.path = path;
            this.used = Arrays.copyOf(used, used.length);
            this.current = current;
        }

        @Override
        protected Boolean compute() {
            if (found.get()) return false;

            // Base case: all nodes visited, check for edge to start
            if (path.size() == N) {
                for (int neighbor : graph.get(current)) {
                    if (neighbor == path.get(0)) {
                        if (found.compareAndSet(false, true)) {
                            synchronized(lock) {
                                solution = new ArrayList<>(path);
                                solution.add(path.get(0)); // close the cycle
                            }
                        }
                        return true;
                    }
                }
                return false;
            }

            // Collect all unused neighbors
            List<Integer> candidates = graph.get(current).stream()
                    .filter(n -> !used[n]).collect(Collectors.toList());
            if (candidates.isEmpty()) return false;

            List<ExploreTask> tasks = new ArrayList<>();
            // Branch among all neighbors in parallel
            for (int neighbor : candidates) {
                if (found.get()) break;
                List<Integer> nextPath = new ArrayList<>(path);
                nextPath.add(neighbor);
                boolean[] nextUsed = Arrays.copyOf(used, used.length);
                nextUsed[neighbor] = true;
                ExploreTask task = new ExploreTask(nextPath, nextUsed, neighbor);
                tasks.add(task);
            }

            // fork() all except one, compute one directly to improve work balance
            boolean result = false;
            int n = tasks.size();
            for (int i = 1; i < n; ++i) {
                tasks.get(i).fork();
            }
            if (n > 0) {
                result = tasks.get(0).compute();
            }
            for (int i = 1; i < n; ++i) {
                result = tasks.get(i).join() || result;
            }
            return result;
        }
    }

    ///////////////////////////
    ///   MAIN SECTION      ///
    ///////////////////////////
    public static void main(String[] args) {
        List<TestGraph> testGraphs = Arrays.asList(
                new TestGraph("Directed 5-cycle", Arrays.asList(
                        Arrays.asList(1,2),
                        Arrays.asList(2,3),
                        Arrays.asList(3,4),
                        Arrays.asList(4,0),
                        Arrays.asList(0,1)
                )),
                new TestGraph("Complete graph with 6 nodes", Arrays.asList(
                        Arrays.asList(1,2,3,4,5), // 0
                        Arrays.asList(0,2,3,4,5), // 1
                        Arrays.asList(0,1,3,4,5), // 2
                        Arrays.asList(0,1,2,4,5), // 3
                        Arrays.asList(0,1,2,3,5), // 4
                        Arrays.asList(0,1,2,3,4)  // 5
                )),
                new TestGraph("Line graph (no cycle)", Arrays.asList(
                        Arrays.asList(1),
                        Arrays.asList(2),
                        Arrays.asList(3),
                        Arrays.asList(4),
                        Arrays.asList()
                )),
                new TestGraph("Wheel graph 7", Arrays.asList(
                        Arrays.asList(1,2,3,4,5,6), // 0 (hub)
                        Arrays.asList(0,2),
                        Arrays.asList(0,3),
                        Arrays.asList(0,4),
                        Arrays.asList(0,5),
                        Arrays.asList(0,6),
                        Arrays.asList(0,1)
                )),
                new TestGraph("Sparse, path but no cycle", Arrays.asList(
                        Arrays.asList(1),
                        Arrays.asList(2),
                        Arrays.asList(3),
                        Arrays.asList(4),
                        Arrays.asList(0)
                ))
        );

        int maxThreads = Runtime.getRuntime().availableProcessors();
        if (maxThreads < 2) maxThreads = 4;
        ForkJoinPool pool = new ForkJoinPool(maxThreads);

        for (TestGraph test : testGraphs) {
            System.out.println("\n===== Test: " + test.name + " =====");
            long start = System.nanoTime();
            HamiltonianForkJoin solver = new HamiltonianForkJoin(test.graph);
            List<Integer> cycle = solver.solve(pool);
            long end = System.nanoTime();
            if (cycle != null) {
                System.out.println("Hamiltonian cycle: " + cycle);
            } else {
                System.out.println("No Hamiltonian cycle found.");
            }
            System.out.printf("Execution time: %.2f ms\n", (end - start) / 1e6);
        }
        pool.shutdown();
    }

    ///////////////////////////
    ///  TEST DATA CLASS    ///
    ///////////////////////////
    private static class TestGraph {
        String name;
        List<List<Integer>> graph;
        TestGraph(String name, List<List<Integer>> graph) {
            this.name = name; this.graph = graph;
        }
    }
}
