///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "constraints.hpp"


///////////////////////////
///     CONSTRAINTS     ///
///////////////////////////
/**
 * @brief Initialize empty timetable state for a given problem instance.
 *
 * Allocates room, professor and group schedules and zeroes professor
 * workload counters.
 */
TimetableState::TimetableState(const ProblemInstance& inst) : inst_(inst) {
    int numRooms = (int)inst.rooms.size();
    int numProfs = (int)inst.professors.size();
    int numGroups = (int)inst.groups.size();

    // Schedules are stored as [resource][day][slot] = activityId / kNone.
    roomSchedule_.assign(numRooms, Grid(DAYS, Row(SLOTS_PER_DAY, kNone)));
    profSchedule_.assign(numProfs, Grid(DAYS, Row(SLOTS_PER_DAY, kNone)));
    groupSchedule_.assign(numGroups, Grid(DAYS, Row(SLOTS_PER_DAY, kNone)));

    // Track total teaching hours per professor (2 hours per activity by design).
    profHours_.assign(numProfs, 0);
}

/**
 * @brief Try to place an activity in a given (day, slot, room).
 *
 * Checks all hard constraints (room/group/prof overlap, travel times,
 * local professor workload upper bound). If placement is valid, updates
 * internal schedules and professor hours and returns true.
 */
bool TimetableState::place(const Activity& act, int day, int slot, int roomIndex) {
    // Bounds check on indices.
    if (day < 0 || day >= DAYS || slot < 0 || slot >= SLOTS_PER_DAY)
        return false;
    if (roomIndex < 0 || roomIndex >= (int)inst_.rooms.size())
        return false;

    int pIdx = profIndex(act.profId);
    if (pIdx < 0) return false;

    // Check room is not already used in this time slot.
    if (!checkRoomFree(roomIndex, day, slot))
        return false;

    // Check that all groups attending are free in this time slot.
    if (!checkGroupsFree(act, day, slot))
        return false;

    // For courses, ensure all groups of the subject are treated consistently.
    if (act.type == ActivityType::COURSE) {
        if (!checkCourseAllGroupsFree(act, day, slot))
            return false;
    }

    // Check professor is not teaching another activity at this time.
    if (!checkProfFree(act, day, slot))
        return false;

    // Check travel-time feasibility relative to adjacent slots.
    if (!checkTravelTimes(act, day, slot, roomIndex))
        return false;

    // Tentatively update professor workload and enforce upper bound locally.
    profHours_[pIdx] += 2; // each activity counts as 2 hours.
    if (!checkProfWorkloadLocal(pIdx)) {
        profHours_[pIdx] -= 2;
        return false;
    }

    // All checks passed: commit this placement into all relevant schedules.
    roomSchedule_[roomIndex][day][slot] = act.id;
    profSchedule_[pIdx][day][slot] = act.id;
    for (int gid : act.groupIds) {
        int gIdx = groupIndex(gid);
        if (gIdx >= 0) {
            groupSchedule_[gIdx][day][slot] = act.id;
        }
    }

    return true;
}

/**
 * @brief Undo a previous placement of an activity.
 *
 * Reverts room, professor and group schedules and subtracts hours from
 * the professor workload counter.
 */
void TimetableState::undo(const Activity& act, int day, int slot, int roomIndex) {
    int pIdx = profIndex(act.profId);
    if (pIdx >= 0) {
        profHours_[pIdx] -= 2;
    }
    if (roomIndex >= 0 && roomIndex < (int)roomSchedule_.size()) {
        roomSchedule_[roomIndex][day][slot] = kNone;
    }
    for (int gid : act.groupIds) {
        int gIdx = groupIndex(gid);
        if (gIdx >= 0) {
            groupSchedule_[gIdx][day][slot] = kNone;
        }
    }
    if (pIdx >= 0) {
        profSchedule_[pIdx][day][slot] = kNone;
    }
}

/**
 * @brief Check global professor workload bounds after full timetable is built.
 *
 * Ensures each professor has at least a minimum number of hours and does not
 * exceed a maximum number of hours in the final solution.
 */
bool TimetableState::checkFinalWorkloadBounds() const {
    int numProfs = (int)inst_.professors.size();
    for (int i = 0; i < numProfs; ++i) {
        int hours = profHours_[i];
        if (hours < 4 || hours > 80) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Check that a room is unused in a given (day, slot).
 */
bool TimetableState::checkRoomFree(int roomIndex, int day, int slot) const {
    return roomSchedule_[roomIndex][day][slot] == kNone;
}

/**
 * @brief Check that all groups of an activity are free in a given slot.
 *
 * Returns false if any group is unknown or already has another activity
 * scheduled at the same time.
 */
bool TimetableState::checkGroupsFree(const Activity& act, int day, int slot) const {
    for (int gid : act.groupIds) {
        int gIdx = groupIndex(gid);
        if (gIdx < 0) return false;
        if (groupSchedule_[gIdx][day][slot] != kNone)
            return false;
    }
    return true;
}

/**
 * @brief Check that the professor of an activity is free in a given slot.
 */
bool TimetableState::checkProfFree(const Activity& act, int day, int slot) const {
    int pIdx = profIndex(act.profId);
    if (pIdx < 0) return false;
    return profSchedule_[pIdx][day][slot] == kNone;
}

/**
 * @brief Check course-specific group constraints (all groups together).
 *
 * For a course, groupIds is assumed to already contain all relevant groups.
 * Currently this is just a placeholder for future extensions; group overlaps
 * are already enforced in checkGroupsFree().
 */
bool TimetableState::checkCourseAllGroupsFree(const Activity& act, int day, int slot) const {
    (void)act;
    (void)day;
    (void)slot;
    return true;
}

/**
 * @brief Enforce travel-time constraints for professor and groups.
 *
 * For the candidate placement, checks adjacent slots (previous and next) for
 * the professor and each attending group. If they have activities in another
 * building, the travel time between buildings must be within the allowed
 * limit (here, <= 10 minutes).
 */
bool TimetableState::checkTravelTimes(const Activity& act, int day, int slot, int roomIndex) const {
    int buildingIdx = roomToBuildingIndex(roomIndex);
    if (buildingIdx < 0) return false;

    // Helper: check a single entity (professor or group) against its schedule.
    auto checkEntity = [&](int entityScheduleIndex, const std::vector<Grid>& schedules) -> bool {
        // Previous slot: entity must be able to travel from previous room to this room.
        if (slot > 0) {
            int actPrevId = schedules[entityScheduleIndex][day][slot - 1];
            if (actPrevId != kNone) {
                // Locate the room used in the previous slot.
                for (int r = 0; r < (int)roomSchedule_.size(); ++r) {
                    if (roomSchedule_[r][day][slot - 1] == actPrevId) {
                        int prevBuildingIdx = roomToBuildingIndex(r);
                        if (prevBuildingIdx < 0) return false;
                        int travel = inst_.travelTime[prevBuildingIdx][buildingIdx];
                        if (travel > 10) return false;
                        break;
                    }
                }
            }
        }
        // Next slot: entity must be able to travel from this room to the next one.
        if (slot < SLOTS_PER_DAY - 1) {
            int actNextId = schedules[entityScheduleIndex][day][slot + 1];
            if (actNextId != kNone) {
                for (int r = 0; r < (int)roomSchedule_.size(); ++r) {
                    if (roomSchedule_[r][day][slot + 1] == actNextId) {
                        int nextBuildingIdx = roomToBuildingIndex(r);
                        if (nextBuildingIdx < 0) return false;
                        int travel = inst_.travelTime[buildingIdx][nextBuildingIdx];
                        if (travel > 10) return false;
                        break;
                    }
                }
            }
        }
        return true;
    };

    // Check travel feasibility for professor.
    int pIdx = profIndex(act.profId);
    if (pIdx < 0) return false;
    if (!checkEntity(pIdx, profSchedule_)) return false;

    // Check travel feasibility for each attending group.
    for (int gid : act.groupIds) {
        int gIdx = groupIndex(gid);
        if (gIdx < 0) return false;
        if (!checkEntity(gIdx, groupSchedule_)) return false;
    }

    return true;
}

/**
 * @brief Local professor workload check used during incremental placement.
 *
 * Only enforces the upper bound (max hours). The lower bound is enforced
 * in checkFinalWorkloadBounds() once a full timetable is built.
 */
bool TimetableState::checkProfWorkloadLocal(int profIndex) const {
    int hours = profHours_[profIndex];
    if (hours > 80) return false;
    return true;
}

/**
 * @brief Map a room index to its building index.
 *
 * Returns -1 if the room index is invalid or if the referenced building
 * id is out of range.
 */
int TimetableState::roomToBuildingIndex(int roomIndex) const {
    if (roomIndex < 0 || roomIndex >= (int)inst_.rooms.size()) return -1;
    int buildingId = inst_.rooms[roomIndex].buildingId;

    // In this model, buildingId is assumed to directly index inst_.buildings.
    if (buildingId < 0 || buildingId >= (int)inst_.buildings.size()) return -1;
    return buildingId;
}

/**
 * @brief Find the index of a professor by id in the instance data.
 *
 * Returns -1 if the professor id is not present.
 */
int TimetableState::profIndex(int profId) const {
    for (int i = 0; i < (int)inst_.professors.size(); ++i)
        if (inst_.professors[i].id == profId)
            return i;
    return -1;
}

/**
 * @brief Find the index of a group by id in the instance data.
 *
 * Returns -1 if the group id is not present.
 */
int TimetableState::groupIndex(int groupId) const {
    for (int i = 0; i < (int)inst_.groups.size(); ++i)
        if (inst_.groups[i].id == groupId)
            return i;
    return -1;
}
