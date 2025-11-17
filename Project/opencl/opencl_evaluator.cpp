///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
#include "opencl_evaluator.hpp"
#include "constraints.hpp"
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

///////////////////////////
///   ERROR  CHECKING   ///
///////////////////////////
static inline void checkError(cl_int err, const char* operation) {
    if (err != CL_SUCCESS) {
        std::stringstream ss;
        ss << "OpenCL error during " << operation << ": " << err;
        throw std::runtime_error(ss.str());
    }
}

///////////////////////////
///   OPENCL KERNELS    ///
///////////////////////////
static const char* TIMETABLE_KERNEL_SRC = R"(
__kernel void eval_timetables(
    __global const int* days,
    __global const int* slots,
    __global const int* rooms,
    const int numCandidates,
    const int numActivities,
    const int numRooms,
    const int numGroups,
    const int numProfs,
    const int numBuildings,
    const int daysPerWeek,
    const int slotsPerDay,
    __global const int* activityGroupOffsets,  // size: numActivities+1
    __global const int* activityGroups,        // flat groupIds
    __global const int* activityProfIds,       // size: numActivities
    __global const int* groupIds,              // size: numGroups
    __global const int* profIds,               // size: numProfs
    __global const int* roomBuildingIndex,     // size: numRooms
    __global int* validOut,
    __global int* scoreOut
) {
    int cid = get_global_id(0);
    if (cid >= numCandidates) return;

    // Conservative limits; adjust if your real instance sizes exceed these.
    const int MAX_GROUPS    = 64;
    const int MAX_PROFS     = 64;
    const int MAX_DAYS      = 7;
    const int MAX_SLOTS     = 16;
    const int MAX_BUILDINGS = 64;

    if (numGroups > MAX_GROUPS ||
        numProfs  > MAX_PROFS  ||
        daysPerWeek > MAX_DAYS ||
        slotsPerDay > MAX_SLOTS ||
        numBuildings > MAX_BUILDINGS) {
        // Instance too big for these static arrays; mark as invalid.
        validOut[cid] = 0;
        scoreOut[cid] = 1000000000;
        return;
    }

    int base = cid * numActivities;

    int valid = 1;
    int score = 0;

    // LATE SLOT PENALTY + bounds
    for (int a = 0; a < numActivities; ++a) {
        int d = days[base + a];
        int s = slots[base + a];

        // Bound checks (days and slots)
        if (d < 0 || d >= daysPerWeek || s < 0 || s >= slotsPerDay) {
            valid = 0;
            break;
        }

        // Late slot penalty: slots 4 and 5 => 16:00-20:00
        if (s >= 4) {
            score += 1;
        }
    }

    if (!valid) {
        validOut[cid] = 0;
        scoreOut[cid] = 1000000000;
        return;
    }

    // BUILD GROUP/PROF OCCUPANCY
    int groupDaySlots[MAX_GROUPS][MAX_DAYS][MAX_SLOTS];
    int profDaySlots [MAX_PROFS][MAX_DAYS][MAX_SLOTS];

    // Initialize occupancy to 0
    for (int g = 0; g < numGroups; ++g)
        for (int d = 0; d < daysPerWeek; ++d)
            for (int s = 0; s < slotsPerDay; ++s)
                groupDaySlots[g][d][s] = 0;

    for (int p = 0; p < numProfs; ++p)
        for (int d = 0; d < daysPerWeek; ++d)
            for (int s = 0; s < slotsPerDay; ++s)
                profDaySlots[p][d][s] = 0;

    // Fill occupancy based on placements and activity metadata
    for (int a = 0; a < numActivities; ++a) {
        int d = days [base + a];
        int s = slots[base + a];

        // Skip "empty" / unused entries; DFS should ensure validity but be defensive.
        if (d < 0 || d >= daysPerWeek || s < 0 || s >= slotsPerDay) {
            continue;
        }

        // Mark all groups attending this activity
        int start = activityGroupOffsets[a];
        int end   = activityGroupOffsets[a + 1];
        for (int gi = start; gi < end; ++gi) {
            int gid = activityGroups[gi];

            // Map group id -> index via linear search (same semantics as CPU)
            int gIdx = -1;
            for (int g = 0; g < numGroups; ++g) {
                if (groupIds[g] == gid) {
                    gIdx = g;
                    break;
                }
            }
            if (gIdx >= 0) {
                groupDaySlots[gIdx][d][s] = 1;
            }
        }

        // Mark professor occupancy
        int profId = activityProfIds[a];
        int pIdx = -1;
        for (int p = 0; p < numProfs; ++p) {
            if (profIds[p] == profId) {
                pIdx = p;
                break;
            }
        }
        if (pIdx >= 0) {
            profDaySlots[pIdx][d][s] = 1;
        }
    }

    // GROUP GAP PENALTY
    for (int g = 0; g < numGroups; ++g) {
        for (int d = 0; d < daysPerWeek; ++d) {
            int first = -1;
            int last  = -1;

            // Find first and last occupied slot
            for (int s = 0; s < slotsPerDay; ++s) {
                if (groupDaySlots[g][d][s]) {
                    if (first == -1) first = s;
                    last = s;
                }
            }

            // No activities or only one activity => no gaps
            if (first == -1 || last == -1 || first == last) {
                continue;
            }

            int gaps = 0;
            for (int s = first; s <= last; ++s) {
                if (!groupDaySlots[g][d][s]) {
                    gaps++;
                }
            }

            // Each idle slot counts as 1 penalty
            score += gaps;
        }
    }

    // PROFESSOR GAP PENALTY
    for (int p = 0; p < numProfs; ++p) {
        for (int d = 0; d < daysPerWeek; ++d) {
            int first = -1;
            int last  = -1;

            for (int s = 0; s < slotsPerDay; ++s) {
                if (profDaySlots[p][d][s]) {
                    if (first == -1) first = s;
                    last = s;
                }
            }

            if (first == -1 || last == -1 || first == last) {
                continue;
            }

            int gaps = 0;
            for (int s = first; s <= last; ++s) {
                if (!profDaySlots[p][d][s]) {
                    gaps++;
                }
            }

            score += gaps;
        }
    }

    // BUILDING LOCALITY PENALTY
    // GROUP-LEVEL: for (group, day), count distinct buildings (>2 => penalty)
    for (int g = 0; g < numGroups; ++g) {
        int groupId = groupIds[g];

        for (int d = 0; d < daysPerWeek; ++d) {
            int usedBuilding[MAX_BUILDINGS];
            for (int i = 0; i < MAX_BUILDINGS; ++i) usedBuilding[i] = 0;

            // Scan all activities in this candidate
            for (int a = 0; a < numActivities; ++a) {
                int ad = days[base + a];
                if (ad != d) continue;

                // Check if this activity is attended by groupId
                int start = activityGroupOffsets[a];
                int end   = activityGroupOffsets[a + 1];
                int attends = 0;
                for (int gi = start; gi < end; ++gi) {
                    if (activityGroups[gi] == groupId) {
                        attends = 1;
                        break;
                    }
                }
                if (!attends) continue;

                int roomIdx = rooms[base + a];
                if (roomIdx < 0 || roomIdx >= numRooms) continue;
                int bIdx = roomBuildingIndex[roomIdx];
                if (bIdx >= 0 && bIdx < MAX_BUILDINGS) {
                    usedBuilding[bIdx] = 1;
                }
            }

            int countBuildings = 0;
            for (int i = 0; i < MAX_BUILDINGS; ++i) {
                if (usedBuilding[i]) countBuildings++;
            }
            if (countBuildings > 2) {
                score += (countBuildings - 2);
            }
        }
    }

    // PROFESSOR-LEVEL: for (prof, day), count distinct buildings (>2 => penalty)
    for (int p = 0; p < numProfs; ++p) {
        int profId = profIds[p];

        for (int d = 0; d < daysPerWeek; ++d) {
            int usedBuilding[MAX_BUILDINGS];
            for (int i = 0; i < MAX_BUILDINGS; ++i) usedBuilding[i] = 0;

            for (int a = 0; a < numActivities; ++a) {
                int ad = days[base + a];
                if (ad != d) continue;

                int aProfId = activityProfIds[a];
                if (aProfId != profId) continue;

                int roomIdx = rooms[base + a];
                if (roomIdx < 0 || roomIdx >= numRooms) continue;
                int bIdx = roomBuildingIndex[roomIdx];
                if (bIdx >= 0 && bIdx < MAX_BUILDINGS) {
                    usedBuilding[bIdx] = 1;
                }
            }

            int countBuildings = 0;
            for (int i = 0; i < MAX_BUILDINGS; ++i) {
                if (usedBuilding[i]) countBuildings++;
            }
            if (countBuildings > 2) {
                score += (countBuildings - 2);
            }
        }
    }

    validOut[cid] = 1;
    scoreOut[cid] = score;
}
)";

///////////////////////////
///  CONTEXT CTOR/DTOR  ///
///////////////////////////
TimetableOpenCLContext::TimetableOpenCLContext() {
    cl_int err = CL_SUCCESS;

    cl_uint numPlatforms = 0;
    err = clGetPlatformIDs(0, nullptr, &numPlatforms);
    checkError(err, "getting platform count");
    if (numPlatforms == 0)
        throw std::runtime_error("No OpenCL platforms found.");

    std::vector<cl_platform_id> platforms(numPlatforms);
    err = clGetPlatformIDs(numPlatforms, platforms.data(), nullptr);
    checkError(err, "getting platform IDs");
    platform = platforms[0];

    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, nullptr);
    if (err != CL_SUCCESS) {
        std::cout << "No GPU found, trying CPU...\n";
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, nullptr);
        checkError(err, "getting device ID");
    }

    char name[256] = {0};
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, nullptr);
    std::cout << "Using OpenCL device: " << name << "\n";

    context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    checkError(err, "creating context");

#if CL_TARGET_OPENCL_VERSION >= 200
    cl_queue_properties props[] = { CL_QUEUE_PROPERTIES, 0, 0 };
    queue = clCreateCommandQueueWithProperties(context, device, props, &err);
#else
    queue = clCreateCommandQueue(context, device, 0, &err);
#endif
    checkError(err, "creating command queue");

    program = buildProgram(TIMETABLE_KERNEL_SRC);
}

TimetableOpenCLContext::~TimetableOpenCLContext() {
    if (queue)   clReleaseCommandQueue(queue);
    if (program) clReleaseProgram(program);
    if (context) clReleaseContext(context);
}

///////////////////////////
///   BUILD PROGRAM     ///
///////////////////////////
cl_program TimetableOpenCLContext::buildProgram(const char* src) {
    cl_int err = CL_SUCCESS;
    size_t len = std::strlen(src);
    const char* srcs[1] = { src };
    size_t lens[1] = { len };

    cl_program prog = clCreateProgramWithSource(context, 1, srcs, lens, &err);
    checkError(err, "creating program from source");

    err = clBuildProgram(prog, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize = 0;
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> log(logSize);
        clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
        std::cerr << "OpenCL build log:\n" << log.data() << "\n";
        throw std::runtime_error("Failed to build OpenCL program");
    }

    return prog;
}

///////////////////////////
///   EVALUATE BATCH    ///
///////////////////////////
void TimetableOpenCLContext::evaluateBatch(
        const ProblemInstance& inst,
        const std::vector<std::vector<Placement>>& batchPlacements,
        std::vector<int>& validFlags,
        std::vector<int>& scores
) {
    cl_int err = CL_SUCCESS;

    int numCandidates = (int)batchPlacements.size();
    if (numCandidates == 0) return;

    int numActivities = (int)inst.activities.size();
    int numRooms = (int)inst.rooms.size();
    int numGroups = (int)inst.groups.size();
    int numProfs = (int)inst.professors.size();
    int numBuildings = (int)inst.buildings.size();
    int daysPerWeek = DAYS;
    int slotsPerDay = SLOTS_PER_DAY;

    // Flatten placements
    std::vector<int> days(numCandidates * numActivities);
    std::vector<int> slots(numCandidates * numActivities);
    std::vector<int> rooms(numCandidates * numActivities);

    for (int c = 0; c < numCandidates; ++c) {
        const auto& placements = batchPlacements[c];
        for (int a = 0; a < numActivities; ++a) {
            const Placement& p = placements[a];
            int idx = c * numActivities + a;
            days [idx] = p.day;
            slots[idx] = p.slot;
            rooms[idx] = p.roomIndex;
        }
    }

    // Flatten instance: activity -> groups (CSR layout)
    std::vector<int> activityGroupOffsets(numActivities + 1);
    std::vector<int> activityGroups;
    activityGroups.reserve(numActivities * 4); // heuristic pre-reserve

    int offset = 0;
    for (int a = 0; a < numActivities; ++a) {
        activityGroupOffsets[a] = offset;
        const Activity& act = inst.activities[a];
        for (int gid : act.groupIds) {
            activityGroups.push_back(gid);
            ++offset;
        }
    }
    activityGroupOffsets[numActivities] = offset;

    // Activity -> professor id
    std::vector<int> activityProfIds(numActivities);
    for (int a = 0; a < numActivities; ++a) {
        activityProfIds[a] = inst.activities[a].profId;
    }

    // Group ids / professor ids
    std::vector<int> groupIds(numGroups);
    for (int g = 0; g < numGroups; ++g) {
        groupIds[g] = inst.groups[g].id;
    }

    std::vector<int> profIds(numProfs);
    for (int p = 0; p < numProfs; ++p) {
        profIds[p] = inst.professors[p].id;
    }

    // Room -> building index
    std::vector<int> roomBuildingIndex(numRooms);
    for (int r = 0; r < numRooms; ++r) {
        roomBuildingIndex[r] = inst.rooms[r].buildingId;
    }

    size_t bufPlacementsSize = (size_t)numCandidates * numActivities * sizeof(int);
    size_t bufOffsetsSize = (size_t)(numActivities + 1) * sizeof(int);
    size_t bufGroupsSize = (size_t)activityGroups.size() * sizeof(int);
    size_t bufGroupIdsSize = (size_t)numGroups * sizeof(int);
    size_t bufProfIdsSize = (size_t)numProfs * sizeof(int);
    size_t bufRoomsSize = (size_t)numRooms * sizeof(int);

    cl_mem d_days  = clCreateBuffer(context, CL_MEM_READ_ONLY,  bufPlacementsSize, nullptr, &err);
    checkError(err, "creating d_days");
    cl_mem d_slots = clCreateBuffer(context, CL_MEM_READ_ONLY,  bufPlacementsSize, nullptr, &err);
    checkError(err, "creating d_slots");
    cl_mem d_rooms = clCreateBuffer(context, CL_MEM_READ_ONLY,  bufPlacementsSize, nullptr, &err);
    checkError(err, "creating d_rooms");

    cl_mem d_valid = clCreateBuffer(context, CL_MEM_WRITE_ONLY, numCandidates * sizeof(int), nullptr, &err);
    checkError(err, "creating d_valid");
    cl_mem d_score = clCreateBuffer(context, CL_MEM_WRITE_ONLY, numCandidates * sizeof(int), nullptr, &err);
    checkError(err, "creating d_score");

    cl_mem d_activityGroupOffsets = clCreateBuffer(context, CL_MEM_READ_ONLY, bufOffsetsSize, nullptr, &err);
    checkError(err, "creating d_activityGroupOffsets");
    cl_mem d_activityGroups = clCreateBuffer(context, CL_MEM_READ_ONLY, bufGroupsSize, nullptr, &err);
    checkError(err, "creating d_activityGroups");
    cl_mem d_activityProfIds = clCreateBuffer(context, CL_MEM_READ_ONLY, numActivities * sizeof(int), nullptr, &err);
    checkError(err, "creating d_activityProfIds");
    cl_mem d_groupIds = clCreateBuffer(context, CL_MEM_READ_ONLY, bufGroupIdsSize, nullptr, &err);
    checkError(err, "creating d_groupIds");
    cl_mem d_profIds = clCreateBuffer(context, CL_MEM_READ_ONLY, bufProfIdsSize, nullptr, &err);
    checkError(err, "creating d_profIds");
    cl_mem d_roomBuildingIndex = clCreateBuffer(context, CL_MEM_READ_ONLY, bufRoomsSize, nullptr, &err);
    checkError(err, "creating d_roomBuildingIndex");

    // Upload data
    err = clEnqueueWriteBuffer(queue, d_days, CL_TRUE, 0, bufPlacementsSize, days.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_days");
    err = clEnqueueWriteBuffer(queue, d_slots, CL_TRUE, 0, bufPlacementsSize, slots.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_slots");
    err = clEnqueueWriteBuffer(queue, d_rooms, CL_TRUE, 0, bufPlacementsSize, rooms.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_rooms");

    err = clEnqueueWriteBuffer(queue, d_activityGroupOffsets, CL_TRUE, 0, bufOffsetsSize, activityGroupOffsets.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_activityGroupOffsets");
    err = clEnqueueWriteBuffer(queue, d_activityGroups, CL_TRUE, 0, bufGroupsSize, activityGroups.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_activityGroups");
    err = clEnqueueWriteBuffer(queue, d_activityProfIds, CL_TRUE, 0, numActivities * sizeof(int), activityProfIds.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_activityProfIds");
    err = clEnqueueWriteBuffer(queue, d_groupIds, CL_TRUE, 0, bufGroupIdsSize, groupIds.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_groupIds");
    err = clEnqueueWriteBuffer(queue, d_profIds, CL_TRUE, 0, bufProfIdsSize, profIds.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_profIds");
    err = clEnqueueWriteBuffer(queue, d_roomBuildingIndex, CL_TRUE, 0, bufRoomsSize, roomBuildingIndex.data(), 0, nullptr, nullptr);
    checkError(err, "writing d_roomBuildingIndex");

    // Kernel + args
    cl_kernel kernel = clCreateKernel(program, "eval_timetables", &err);
    checkError(err, "creating kernel");

    int arg = 0;
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_days); checkError(err, "arg days");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_slots); checkError(err, "arg slots");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_rooms); checkError(err, "arg rooms");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &numCandidates); checkError(err, "arg numCandidates");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &numActivities); checkError(err, "arg numActivities");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &numRooms); checkError(err, "arg numRooms");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &numGroups); checkError(err, "arg numGroups");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &numProfs); checkError(err, "arg numProfs");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &numBuildings); checkError(err, "arg numBuildings");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &daysPerWeek); checkError(err, "arg daysPerWeek");
    err = clSetKernelArg(kernel, arg++, sizeof(int), &slotsPerDay); checkError(err, "arg slotsPerDay");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_activityGroupOffsets);checkError(err, "arg activityGroupOffsets");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_activityGroups); checkError(err, "arg activityGroups");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_activityProfIds); checkError(err, "arg activityProfIds");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_groupIds); checkError(err, "arg groupIds");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_profIds); checkError(err, "arg profIds");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_roomBuildingIndex); checkError(err, "arg roomBuildingIndex");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_valid); checkError(err, "arg validOut");
    err = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &d_score); checkError(err, "arg scoreOut");

    size_t global = (size_t)numCandidates;
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
    checkError(err, "enqueuing eval_timetables");
    err = clFinish(queue);
    checkError(err, "finishing queue");

    validFlags.resize(numCandidates);
    scores.resize(numCandidates);

    err = clEnqueueReadBuffer(queue, d_valid, CL_TRUE, 0, numCandidates * sizeof(int), validFlags.data(), 0, nullptr, nullptr);
    checkError(err, "reading validFlags");
    err = clEnqueueReadBuffer(queue, d_score, CL_TRUE, 0, numCandidates * sizeof(int), scores.data(), 0, nullptr, nullptr);
    checkError(err, "reading scores");

    clReleaseKernel(kernel);
    clReleaseMemObject(d_days);
    clReleaseMemObject(d_slots);
    clReleaseMemObject(d_rooms);
    clReleaseMemObject(d_valid);
    clReleaseMemObject(d_score);
    clReleaseMemObject(d_activityGroupOffsets);
    clReleaseMemObject(d_activityGroups);
    clReleaseMemObject(d_activityProfIds);
    clReleaseMemObject(d_groupIds);
    clReleaseMemObject(d_profIds);
    clReleaseMemObject(d_roomBuildingIndex);
}
