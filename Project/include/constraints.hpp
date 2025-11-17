#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include <vector>


///////////////////////////
///     CONSTRAINTS     ///
///////////////////////////
/**
 * @brief Placement of a single activity in the timetable grid.
 *
 * Associates an activity id with a specific (day, slot) and a concrete room.
 */
struct Placement {
    int activityId; ///< Id of the activity being scheduled (or -1 if unused).
    int day; ///< Day index in the timetable (0..DAYS-1).
    int slot; ///< Time slot index within the day (0..SLOTS_PER_DAY-1).
    int roomIndex; ///< Index into ProblemInstance::rooms for the assigned room.
};

/**
 * @brief Incremental state of a candidate timetable during search.
 *
 * Tracks room, professor and group occupancy over the time grid, and enforces
 * all hard constraints when placing or undoing activities:
 *  - no overlaps for rooms, professors and groups,
 *  - course activities must involve all required groups,
 *  - travel times between buildings for consecutive slots must be feasible,
 *  - professor workload must stay within allowed bounds.
 */
class TimetableState {
public:
    /**
     * @brief Construct an empty timetable state for a given instance.
     *
     * Initializes room/professor/group schedules and zeroes per-professor
     * workload counters.
     */
    explicit TimetableState(const ProblemInstance& inst);

    /**
     * @brief Try to place an activity at a given (day, slot, room).
     *
     * Checks all hard constraints (room/professor/group overlaps, course
     * semantics, travel-time feasibility and local professor workload upper
     * bound). On success, updates internal schedules and returns true; on
     * failure, leaves the state unchanged and returns false.
     *
     * @param act       Activity to place.
     * @param day       Day index in the timetable.
     * @param slot      Slot index within the day.
     * @param roomIndex Index into inst.rooms for the chosen room.
     */
    bool place(const Activity& act, int day, int slot, int roomIndex);

    /**
     * @brief Undo a previously successful placement of an activity.
     *
     * Reverts room, professor and group schedules, and subtracts the
     * corresponding hours from the professor workload counter.
     */
    void undo(const Activity& act, int day, int slot, int roomIndex);

    /**
     * @brief Check final professor workload bounds over the full timetable.
     *
     * Used once a complete timetable is built to enforce the global workload
     * range (e.g., minimum and maximum teaching hours per professor).
     *
     * @return true if all professor workloads are within allowed bounds, false otherwise.
     */
    bool checkFinalWorkloadBounds() const;

    /**
     * @brief Access the underlying problem instance.
     *
     * Useful for scoring or debugging routines that need static instance data.
     */
    const ProblemInstance& instance() const { return inst_; }

private:
    /// Reference to the problem instance this state belongs to.
    const ProblemInstance& inst_;

    /// Sentinel used to mark empty/unassigned schedule entries.
    static constexpr int kNone = -1;

    using Row  = std::vector<int>;
    using Grid = std::vector<Row>;

    /// roomSchedule[roomIndex][day][slot] = activityId or kNone.
    std::vector<Grid> roomSchedule_;

    /// profSchedule[profIndex][day][slot] = activityId or kNone.
    std::vector<Grid> profSchedule_;

    /// groupSchedule[groupIndex][day][slot] = activityId or kNone.
    std::vector<Grid> groupSchedule_;

    /// Current workload per professor in hours (each activity counts as 2h).
    std::vector<int> profHours_;

    /**
     * @brief Check whether a room is free at (day, slot).
     */
    bool checkRoomFree(int roomIndex, int day, int slot) const;

    /**
     * @brief Check whether all groups of an activity are free at (day, slot).
     */
    bool checkGroupsFree(const Activity& act, int day, int slot) const;

    /**
     * @brief Check whether the professor of an activity is free at (day, slot).
     */
    bool checkProfFree(const Activity& act, int day, int slot) const;

    /**
     * @brief Check course-specific group conditions.
     *
     * For course activities, groupIds is assumed to contain all relevant
     * groups; this hook exists for clarity and potential extensions.
     */
    bool checkCourseAllGroupsFree(const Activity& act, int day, int slot) const;

    /**
     * @brief Check travel-time feasibility for professor and groups.
     *
     * For the candidate placement, ensures that any activities in adjacent
     * slots (previous/next) for the same professor or groups are reachable
     * within the allowed travel time between buildings.
     */
    bool checkTravelTimes(const Activity& act, int day, int slot, int roomIndex) const;

    /**
     * @brief Check local (incremental) upper bound on professor workload.
     *
     * Enforces only the maximum hours constraint while building the timetable;
     * the minimum is checked globally in checkFinalWorkloadBounds().
     */
    bool checkProfWorkloadLocal(int profIndex) const;

    /**
     * @brief Map a room index to a building index, or -1 if invalid.
     */
    int roomToBuildingIndex(int roomIndex) const;

    /**
     * @brief Find professor index in instance data by professor id.
     *
     * @return 0-based index in inst_.professors, or -1 if not found.
     */
    int profIndex(int profId) const;

    /**
     * @brief Find group index in instance data by group id.
     *
     * @return 0-based index in inst_.groups, or -1 if not found.
     */
    int groupIndex(int groupId) const;
};
