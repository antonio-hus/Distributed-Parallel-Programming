///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <sstream>

///////////////////////////
///   TYPEDEFS SECTION  ///
///////////////////////////
using Coeff = int;
using Poly  = std::vector<Coeff>;

///////////////////////////
///   ERROR  CHECKING   ///
///////////////////////////
inline void checkError(cl_int err, const char* operation) {
    if (err != CL_SUCCESS) {
        std::stringstream ss;
        ss << "OpenCL error during " << operation << ": " << err;
        throw std::runtime_error(ss.str());
    }
}

///////////////////////////
///   OPENCL  KERNELS   ///
///////////////////////////

/**
 * Naive O(n^2) polynomial multiplication kernel.
 * Algorithm: Each work-item computes one coefficient result[idx] by summing
 * all products a[i]*b[j] where i+j=idx.
 * Synchronization: None needed - each work-item writes to a unique location.
 */
static const char* KERNEL_SOURCE_NAIVE = R"(
__kernel void poly_multiply_naive(
    __global const int* a,
    __global const int* b,
    __global int* result,
    const int n,
    const int m
) {
    int idx = get_global_id(0);
    int result_size = n + m - 1;
    if (idx >= result_size) return;

    int sum = 0;
    int i_min = (idx >= m) ? (idx - m + 1) : 0;
    int i_max = (idx < n) ? idx : (n - 1);

    for (int i = i_min; i <= i_max; ++i) {
        int j = idx - i;
        sum += a[i] * b[j];
    }
    result[idx] = sum;
}
)";

/**
 * Local memory version - copies input polynomials to local memory first.
 * Algorithm: Same as naive but uses faster local memory for reads.
 * Synchronization: barrier(CLK_LOCAL_MEM_FENCE) ensures all work-items
 * in the work-group have finished loading data before computation starts.
 */
static const char* KERNEL_SOURCE_LOCAL = R"(
__kernel void poly_multiply_local(
    __global const int* a,
    __global const int* b,
    __global int* result,
    __local int* local_a,
    __local int* local_b,
    const int n,
    const int m
) {
    int gid   = get_global_id(0);
    int lid   = get_local_id(0);
    int lsize = get_local_size(0);
    int result_size = n + m - 1;

    // Cooperative loading: each work-item loads multiple elements
    for (int i = lid; i < n; i += lsize) {
        local_a[i] = a[i];
    }
    for (int i = lid; i < m; i += lsize) {
        local_b[i] = b[i];
    }

    // Synchronization: wait for all loads to complete
    barrier(CLK_LOCAL_MEM_FENCE);

    if (gid < result_size) {
        int sum = 0;
        int i_min = (gid >= m) ? (gid - m + 1) : 0;
        int i_max = (gid < n) ? gid : (n - 1);

        for (int i = i_min; i <= i_max; ++i) {
            int j = gid - i;
            sum += local_a[i] * local_b[j];
        }
        result[gid] = sum;
    }
}
)";

/**
 * Karatsuba combination kernel.
 * Algorithm: Combines P1, P2, P3 into final result:
 * result = P1 + (P3-P1-P2)*x^split_point + P2*x^(2*split_point)
 * Synchronization: None needed - each work-item writes to unique location.
 */
static const char* KERNEL_SOURCE_KARATSUBA_COMBINE = R"(
__kernel void karatsuba_combine(
    __global const int* P1,
    __global const int* P2,
    __global const int* P3,
    __global int* result,
    const int P1_size,
    const int P2_size,
    const int P3_size,
    const int split_point,
    const int result_size
) {
    int idx = get_global_id(0);
    if (idx >= result_size) return;

    int val = 0;

    // Add P1
    if (idx < P1_size) {
        val += P1[idx];
    }

    // Add P3 at offset split_point
    if (idx >= split_point && idx - split_point < P3_size) {
        val += P3[idx - split_point];
    }

    // Subtract P1 at offset split_point
    if (idx >= split_point && idx - split_point < P1_size) {
        val -= P1[idx - split_point];
    }

    // Subtract P2 at offset split_point
    if (idx >= split_point && idx - split_point < P2_size) {
        val -= P2[idx - split_point];
    }

    // Add P2 at offset 2*split_point
    if (idx >= 2*split_point && idx - 2*split_point < P2_size) {
        val += P2[idx - 2*split_point];
    }

    result[idx] = val;
}
)";

///////////////////////////
///    CPU BASELINES    ///
///////////////////////////

static Poly multiply_naive_cpu(const Poly& a, const Poly& b) {
    size_t n = a.size(), m = b.size();
    Poly result(n + m - 1, 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < m; ++j)
            result[i + j] += a[i] * b[j];
    return result;
}

static Poly multiply_karatsuba_cpu(const Poly& a, const Poly& b) {
    size_t n = a.size(), m = b.size();

    if (n <= 64 || m <= 64)
        return multiply_naive_cpu(a, b);

    size_t half = n / 2;
    Poly a_low(a.begin(), a.begin() + half);
    Poly a_high(a.begin() + half, a.end());
    Poly b_low(b.begin(), b.begin() + std::min(half, m));
    Poly b_high(b.begin() + std::min(half, m), b.end());

    Poly P1 = multiply_karatsuba_cpu(a_low, b_low);
    Poly P2 = multiply_karatsuba_cpu(a_high, b_high);

    Poly a_sum(std::max(a_low.size(), a_high.size()), 0);
    Poly b_sum(std::max(b_low.size(), b_high.size()), 0);
    for (size_t i = 0; i < a_low.size(); ++i) a_sum[i] += a_low[i];
    for (size_t i = 0; i < a_high.size(); ++i) a_sum[i] += a_high[i];
    for (size_t i = 0; i < b_low.size(); ++i) b_sum[i] += b_low[i];
    for (size_t i = 0; i < b_high.size(); ++i) b_sum[i] += b_high[i];

    Poly P3 = multiply_karatsuba_cpu(a_sum, b_sum);

    for (size_t i = 0; i < P1.size(); ++i) P3[i] -= P1[i];
    for (size_t i = 0; i < P2.size(); ++i) P3[i] -= P2[i];

    Poly result(n + m - 1, 0);
    for (size_t i = 0; i < P1.size(); ++i) result[i] += P1[i];
    for (size_t i = 0; i < P3.size(); ++i) result[i + half] += P3[i];
    for (size_t i = 0; i < P2.size(); ++i) result[i + 2 * half] += P2[i];
    return result;
}

///////////////////////////
///   OPENCL  CONTEXT   ///
///////////////////////////

class OpenCLContext {
public:
    cl_platform_id   platform   = nullptr;
    cl_device_id     device     = nullptr;
    cl_context       context    = nullptr;
    cl_program       program_naive = nullptr;
    cl_program       program_local = nullptr;
    cl_program       program_karatsuba_comb = nullptr;
    cl_command_queue queue      = nullptr;
    size_t           max_work_group_size = 0;
    cl_uint          compute_units = 0;

    OpenCLContext() {
        cl_int err = CL_SUCCESS;

        // 1. Platform
        cl_uint numPlatforms = 0;
        err = clGetPlatformIDs(0, nullptr, &numPlatforms);
        checkError(err, "getting platform count");
        if (numPlatforms == 0)
            throw std::runtime_error("No OpenCL platforms found.");

        std::vector<cl_platform_id> platforms(numPlatforms);
        err = clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);
        checkError(err, "getting platform IDs");
        platform = platforms[0];

        // 2. Device (GPU preferred, fallback CPU)
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
        if (err != CL_SUCCESS) {
            std::cout << "No GPU found, trying CPU...\n";
            err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, nullptr);
            checkError(err, "getting device ID");
        }

        char name[256] = {0};
        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, nullptr);
        std::cout << "Using OpenCL device: " << name << "\n";

        // Query device capabilities
        clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                        sizeof(max_work_group_size), &max_work_group_size, nullptr);
        clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS,
                        sizeof(compute_units), &compute_units, nullptr);

        std::cout << "Device max work group size: " << max_work_group_size << "\n";
        std::cout << "Device compute units (EU): " << compute_units << "\n";

        // 3. Context
        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        checkError(err, "creating context");

        // 4. Command queue
#if CL_TARGET_OPENCL_VERSION >= 200
        cl_queue_properties props[] = { CL_QUEUE_PROPERTIES, 0, 0 };
        queue = clCreateCommandQueueWithProperties(context, device, props, &err);
#else
        queue = clCreateCommandQueue(context, device, 0, &err);
#endif
        checkError(err, "creating command queue");

        // 5. Build programs
        program_naive = buildProgram(KERNEL_SOURCE_NAIVE);
        program_local = buildProgram(KERNEL_SOURCE_LOCAL);
        program_karatsuba_comb = buildProgram(KERNEL_SOURCE_KARATSUBA_COMBINE);
    }

    ~OpenCLContext() {
        if (queue)         clReleaseCommandQueue(queue);
        if (program_naive) clReleaseProgram(program_naive);
        if (program_local) clReleaseProgram(program_local);
        if (program_karatsuba_comb) clReleaseProgram(program_karatsuba_comb);
        if (context)       clReleaseContext(context);
    }

    // Helper to get kernel-specific max work group size
    size_t getKernelWorkGroupSize(cl_kernel kernel) {
        size_t kernel_wg_size = 0;
        cl_int err = clGetKernelWorkGroupInfo(kernel, device,
                                              CL_KERNEL_WORK_GROUP_SIZE,
                                              sizeof(kernel_wg_size),
                                              &kernel_wg_size, nullptr);
        checkError(err, "getting kernel work group size");
        return kernel_wg_size;
    }

    // Helper to compute appropriate local work size
    size_t computeLocalSize(size_t kernel_max_wg_size) {
        // Prefer larger work group sizes for better occupancy
        const size_t candidates[] = {256, 128, 64, 32, 16, 8, 4, 1};

        for (size_t candidate : candidates) {
            if (candidate <= kernel_max_wg_size && candidate <= max_work_group_size) {
                return candidate;
            }
        }
        return 1;
    }

    // Helper to round up to next multiple
    size_t roundUp(size_t value, size_t multiple) {
        return ((value + multiple - 1) / multiple) * multiple;
    }

    Poly multiply_naive(const Poly& a, const Poly& b) {
        cl_int err = CL_SUCCESS;
        int n = static_cast<int>(a.size());
        int m = static_cast<int>(b.size());
        int result_size = n + m - 1;

        size_t sizeA = n * sizeof(Coeff);
        size_t sizeB = m * sizeof(Coeff);
        size_t sizeR = result_size * sizeof(Coeff);

        cl_mem d_a = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeA, nullptr, &err);
        checkError(err, "creating buffer d_a");
        cl_mem d_b = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeB, nullptr, &err);
        checkError(err, "creating buffer d_b");
        cl_mem d_r = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeR, nullptr, &err);
        checkError(err, "creating buffer d_r");

        err = clEnqueueWriteBuffer(queue, d_a, CL_TRUE, 0, sizeA, a.data(), 0, nullptr, nullptr);
        checkError(err, "writing buffer d_a");
        err = clEnqueueWriteBuffer(queue, d_b, CL_TRUE, 0, sizeB, b.data(), 0, nullptr, nullptr);
        checkError(err, "writing buffer d_b");

        cl_kernel kernel = clCreateKernel(program_naive, "poly_multiply_naive", &err);
        checkError(err, "creating kernel");

        size_t kernel_max_wg = getKernelWorkGroupSize(kernel);

        err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_a);
        checkError(err, "setting kernel arg 0");
        err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_b);
        checkError(err, "setting kernel arg 1");
        err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_r);
        checkError(err, "setting kernel arg 2");
        err = clSetKernelArg(kernel, 3, sizeof(int), &n);
        checkError(err, "setting kernel arg 3");
        err = clSetKernelArg(kernel, 4, sizeof(int), &m);
        checkError(err, "setting kernel arg 4");

        // Round up global size to multiple of local size
        size_t local = computeLocalSize(kernel_max_wg);
        size_t global = roundUp(static_cast<size_t>(result_size), local);

        err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr);
        checkError(err, "enqueuing kernel");
        err = clFinish(queue);
        checkError(err, "finishing queue");

        Poly result(result_size);
        err = clEnqueueReadBuffer(queue, d_r, CL_TRUE, 0, sizeR, result.data(), 0, nullptr, nullptr);
        checkError(err, "reading buffer d_r");

        clReleaseKernel(kernel);
        clReleaseMemObject(d_a);
        clReleaseMemObject(d_b);
        clReleaseMemObject(d_r);

        return result;
    }

    Poly multiply_local(const Poly& a, const Poly& b) {
        cl_int err = CL_SUCCESS;
        int n = static_cast<int>(a.size());
        int m = static_cast<int>(b.size());
        int result_size = n + m - 1;

        size_t sizeA = n * sizeof(Coeff);
        size_t sizeB = m * sizeof(Coeff);
        size_t sizeR = result_size * sizeof(Coeff);

        cl_ulong local_mem = 0;
        clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(local_mem), &local_mem, nullptr);
        size_t needed = (static_cast<size_t>(n) + m) * sizeof(Coeff);
        if (needed > local_mem) {
            std::cerr << "Warning: not enough local mem (" << needed << " needed, "
                      << local_mem << " available), falling back to naive\n";
            return multiply_naive(a, b);
        }

        cl_mem d_a = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeA, nullptr, &err);
        checkError(err, "creating buffer d_a (local)");
        cl_mem d_b = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeB, nullptr, &err);
        checkError(err, "creating buffer d_b (local)");
        cl_mem d_r = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeR, nullptr, &err);
        checkError(err, "creating buffer d_r (local)");

        err = clEnqueueWriteBuffer(queue, d_a, CL_TRUE, 0, sizeA, a.data(), 0, nullptr, nullptr);
        checkError(err, "writing buffer d_a (local)");
        err = clEnqueueWriteBuffer(queue, d_b, CL_TRUE, 0, sizeB, b.data(), 0, nullptr, nullptr);
        checkError(err, "writing buffer d_b (local)");

        cl_kernel kernel = clCreateKernel(program_local, "poly_multiply_local", &err);
        checkError(err, "creating kernel (local)");

        size_t kernel_max_wg = getKernelWorkGroupSize(kernel);

        err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_a);
        checkError(err, "setting kernel arg 0 (local)");
        err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_b);
        checkError(err, "setting kernel arg 1 (local)");
        err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_r);
        checkError(err, "setting kernel arg 2 (local)");
        err = clSetKernelArg(kernel, 3, n * sizeof(Coeff), nullptr);
        checkError(err, "setting kernel arg 3 (local)");
        err = clSetKernelArg(kernel, 4, m * sizeof(Coeff), nullptr);
        checkError(err, "setting kernel arg 4 (local)");
        err = clSetKernelArg(kernel, 5, sizeof(int), &n);
        checkError(err, "setting kernel arg 5 (local)");
        err = clSetKernelArg(kernel, 6, sizeof(int), &m);
        checkError(err, "setting kernel arg 6 (local)");

        size_t local = computeLocalSize(kernel_max_wg);
        size_t global = roundUp(static_cast<size_t>(result_size), local);

        err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr);
        checkError(err, "enqueuing kernel (local)");
        err = clFinish(queue);
        checkError(err, "finishing queue (local)");

        Poly result(result_size);
        err = clEnqueueReadBuffer(queue, d_r, CL_TRUE, 0, sizeR, result.data(), 0, nullptr, nullptr);
        checkError(err, "reading buffer d_r (local)");

        clReleaseKernel(kernel);
        clReleaseMemObject(d_a);
        clReleaseMemObject(d_b);
        clReleaseMemObject(d_r);

        return result;
    }

    /**
     * GPU Karatsuba Implementation with depth limiting (internal)
     * Algorithm: Splits polynomials in half and computes:
     * - P1 = low1 * low2
     * - P2 = high1 * high2
     * - P3 = (low1+high1) * (low2+high2)
     * - result = P1 + (P3-P1-P2)*x^split_point + P2*x^(2*split_point)
     *
     * Recursion is limited to max_depth levels to avoid excessive overhead.
     * Base cases use GPU naive multiplication.
     * Synchronization: Recursive calls implicitly synchronized via clFinish().
     */
    Poly multiply_karatsuba_impl(const Poly& a, const Poly& b, int depth, int max_depth) {
        size_t n = a.size();
        size_t m = b.size();

        // Base case: use naive GPU for small polynomials or max depth reached
        if (n <= 512 || m <= 512 || depth >= max_depth) {
            return multiply_naive(a, b);
        }

        cl_int err = CL_SUCCESS;
        size_t split_pos = n / 2;

        // Split polynomials
        Poly a_low(a.begin(), a.begin() + split_pos);
        Poly a_high(a.begin() + split_pos, a.end());
        Poly b_low(b.begin(), b.begin() + std::min(split_pos, m));
        Poly b_high(b.begin() + std::min(split_pos, m), b.end());

        // Compute sums
        Poly a_sum(std::max(a_low.size(), a_high.size()), 0);
        Poly b_sum(std::max(b_low.size(), b_high.size()), 0);
        for (size_t i = 0; i < a_low.size(); ++i) a_sum[i] += a_low[i];
        for (size_t i = 0; i < a_high.size(); ++i) a_sum[i] += a_high[i];
        for (size_t i = 0; i < b_low.size(); ++i) b_sum[i] += b_low[i];
        for (size_t i = 0; i < b_high.size(); ++i) b_sum[i] += b_high[i];

        // Compute P1, P2, P3 recursively on GPU
        Poly P1 = multiply_karatsuba_impl(a_low, b_low, depth + 1, max_depth);
        Poly P2 = multiply_karatsuba_impl(a_high, b_high, depth + 1, max_depth);
        Poly P3 = multiply_karatsuba_impl(a_sum, b_sum, depth + 1, max_depth);

        // Combine on GPU using combination kernel
        int P1_size = static_cast<int>(P1.size());
        int P2_size = static_cast<int>(P2.size());
        int P3_size = static_cast<int>(P3.size());
        int result_size = static_cast<int>(n + m - 1);

        // Create buffers
        cl_mem d_P1 = clCreateBuffer(context, CL_MEM_READ_ONLY, P1_size * sizeof(Coeff), nullptr, &err);
        checkError(err, "creating P1 buffer");
        cl_mem d_P2 = clCreateBuffer(context, CL_MEM_READ_ONLY, P2_size * sizeof(Coeff), nullptr, &err);
        checkError(err, "creating P2 buffer");
        cl_mem d_P3 = clCreateBuffer(context, CL_MEM_READ_ONLY, P3_size * sizeof(Coeff), nullptr, &err);
        checkError(err, "creating P3 buffer");
        cl_mem d_result = clCreateBuffer(context, CL_MEM_WRITE_ONLY, result_size * sizeof(Coeff), nullptr, &err);
        checkError(err, "creating result buffer");

        // Upload data
        clEnqueueWriteBuffer(queue, d_P1, CL_TRUE, 0, P1_size * sizeof(Coeff), P1.data(), 0, nullptr, nullptr);
        clEnqueueWriteBuffer(queue, d_P2, CL_TRUE, 0, P2_size * sizeof(Coeff), P2.data(), 0, nullptr, nullptr);
        clEnqueueWriteBuffer(queue, d_P3, CL_TRUE, 0, P3_size * sizeof(Coeff), P3.data(), 0, nullptr, nullptr);

        // Create and execute combination kernel
        cl_kernel kernel = clCreateKernel(program_karatsuba_comb, "karatsuba_combine", &err);
        checkError(err, "creating combine kernel");

        int split_point = static_cast<int>(split_pos);
        clSetKernelArg(kernel, 0, sizeof(cl_mem), &d_P1);
        clSetKernelArg(kernel, 1, sizeof(cl_mem), &d_P2);
        clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_P3);
        clSetKernelArg(kernel, 3, sizeof(cl_mem), &d_result);
        clSetKernelArg(kernel, 4, sizeof(int), &P1_size);
        clSetKernelArg(kernel, 5, sizeof(int), &P2_size);
        clSetKernelArg(kernel, 6, sizeof(int), &P3_size);
        clSetKernelArg(kernel, 7, sizeof(int), &split_point);
        clSetKernelArg(kernel, 8, sizeof(int), &result_size);

        size_t kernel_max_wg = getKernelWorkGroupSize(kernel);
        size_t local = computeLocalSize(kernel_max_wg);
        size_t global = roundUp(static_cast<size_t>(result_size), local);

        clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, &local, 0, nullptr, nullptr);
        clFinish(queue);

        // Download result
        Poly result(result_size);
        clEnqueueReadBuffer(queue, d_result, CL_TRUE, 0, result_size * sizeof(Coeff), result.data(), 0, nullptr, nullptr);

        // Cleanup
        clReleaseKernel(kernel);
        clReleaseMemObject(d_P1);
        clReleaseMemObject(d_P2);
        clReleaseMemObject(d_P3);
        clReleaseMemObject(d_result);

        return result;
    }

    /**
     * Public interface for Karatsuba GPU multiplication
     * Limited to 3 recursion levels to balance parallelism and overhead
     */
    Poly multiply_karatsuba(const Poly& a, const Poly& b) {
        const int MAX_DEPTH = 3;
        return multiply_karatsuba_impl(a, b, 0, MAX_DEPTH);
    }

private:
    cl_program buildProgram(const char* source) {
        cl_int err = CL_SUCCESS;
        size_t lengths[1] = { std::strlen(source) };
        const char* sources[1] = { source };

        cl_program prog = clCreateProgramWithSource(context, 1, sources, lengths, &err);
        checkError(err, "creating program from source");

        err = clBuildProgram(prog, 1, &device, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t log_size = 0;
            clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::vector<char> log(log_size);
            clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
            std::cerr << "Build log:\n" << log.data() << "\n";
            throw std::runtime_error("Failed to build OpenCL program.");
        }
        return prog;
    }
};

///////////////////////////
/// BENCHMARKING SECTION///
///////////////////////////
static void benchmark(const std::string& name,
                      const std::function<Poly(const Poly&, const Poly&)>& fn,
                      const Poly& A,
                      const Poly& B) {
    auto start  = std::chrono::high_resolution_clock::now();
    Poly result = fn(A, B);
    auto end    = std::chrono::high_resolution_clock::now();
    double ms   = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << std::left << std::setw(35) << name
              << " -> Time: " << std::fixed << std::setprecision(4) << std::setw(10) << ms
              << " ms  |  Result[0..4]: ";
    for (size_t i = 0; i < std::min<size_t>(5, result.size()); ++i)
        std::cout << result[i] << " ";
    std::cout << "\n";
}

///////////////////////////
///   MAIN SECTION      ///
///////////////////////////
int main() {
    try {
        OpenCLContext cl_ctx;

        const size_t n = 1 << 16;
        Poly A(n), B(n);
        for (size_t i = 0; i < n; ++i) {
            A[i] = (i % 10) + 1;
            B[i] = (i % 5) + 2;
        }

        std::cout << "\n========================================\n";
        std::cout << "GPU POLYNOMIAL MULTIPLICATION BENCHMARK\n";
        std::cout << "========================================\n";
        std::cout << "Polynomial degree: " << n << "\n";
        std::cout << "Result size: " << (2*n - 1) << " coefficients\n";
        std::cout << "========================================\n";

        std::cout << "\n--- CPU Baselines ---\n";
        benchmark("Naive CPU (O(n^2))",multiply_naive_cpu, A, B);
        benchmark("Karatsuba CPU (O(n^1.58))",multiply_karatsuba_cpu, A, B);

        std::cout << "\n--- GPU Implementations (OpenCL) ---\n";
        benchmark("Naive GPU - Global Memory",
                  [&](const Poly& a, const Poly& b) { return cl_ctx.multiply_naive(a, b); },
                  A, B);
        benchmark("Naive GPU - Local Memory",
                  [&](const Poly& a, const Poly& b) { return cl_ctx.multiply_local(a, b); },
                  A, B);
        benchmark("Karatsuba GPU (3-level depth)",
                  [&](const Poly& a, const Poly& b) { return cl_ctx.multiply_karatsuba(a, b); },
                  A, B);

        std::cout << "\n========================================\n";
        std::cout << "All tests completed successfully!\n";
        std::cout << "========================================\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
