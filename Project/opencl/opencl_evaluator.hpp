#pragma once
#define CL_TARGET_OPENCL_VERSION 120

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include <CL/cl.h>
#include <vector>
#include "model.hpp"
#include "constraints.hpp"


///////////////////////////
///      EVALUATOR      ///
///////////////////////////
/**
 * @brief OpenCL helper context for batched timetable evaluation.
 *
 * Owns the OpenCL platform/device/context/queue and a compiled program
 * used to score many complete timetables in parallel on the GPU.
 */
class TimetableOpenCLContext {
public:
    /**
     * @brief Initialize OpenCL platform, device, context and command queue.
     *
     * Also builds the OpenCL program containing the timetable scoring kernel.
     * Throws or terminates if OpenCL setup fails.
     */
    TimetableOpenCLContext();

    /**
     * @brief Release all OpenCL resources owned by this context.
     *
     * Destroys program, queue and context in the correct order.
     */
    ~TimetableOpenCLContext();

    /**
     * @brief Evaluate a batch of complete timetables on the GPU.
     *
     * Each element of batchPlacements is a full placements vector of size
     * inst.activities.size(), representing a complete timetable candidate.
     *
     * On return:
     *  - validFlags[i] is 1 if candidate i passes structural checks
     *    (day/slot bounds) and can be scored, 0 otherwise.
     *  - scores[i] is the soft-constraint score for candidate i, computed
     *    using the same components as CPU computeScore():
     *      * late slot penalties,
     *      * group gap penalties,
     *      * professor gap penalties,
     *      * building locality penalties.
     *
     * All hard constraints are assumed to be already enforced on the CPU
     * while generating candidates.
     */
    void evaluateBatch(
            const ProblemInstance& inst,
            const std::vector<std::vector<Placement>>& batchPlacements,
            std::vector<int>& validFlags,
            std::vector<int>& scores
    );

private:
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_program program = nullptr;
    cl_command_queue queue = nullptr;

    cl_program buildProgram(const char* src);
};
