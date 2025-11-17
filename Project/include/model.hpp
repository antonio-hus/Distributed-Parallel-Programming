#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include <string>
#include <vector>


///////////////////////////
///       MODELS        ///
///////////////////////////
/**
 * @brief Physical building where rooms are located.
 */
struct Building {
    int id; ///< Unique building identifier.
    std::string name; ///< Human-readable building name.
};

/**
 * @brief A single teaching room in a building.
 */
struct Room {
    int id; ///< Unique room identifier.
    int buildingId; ///< Id of the building this room belongs to.
    std::string name; ///< Room name/label (e.g., "C301").
    int capacity; ///< Maximum number of students the room can hold.

    /// Room type used to enforce compatibility with activity type.
    enum class Type { COURSE, SEMINAR, LAB } type;
};

/**
 * @brief A subject with required weekly teaching slots.
 *
 * Each subject specifies how many 2-hour course/seminar/lab sessions
 * must be scheduled across the week.
 */
struct Subject {
    int id; ///< Unique subject identifier.
    std::string name; ///< Human-readable subject name.
    int courseSlots; ///< Number of 2h course sessions per week.
    int seminarSlots; ///< Number of 2h seminar sessions per week.
    int labSlots; ///< Number of 2h lab sessions per week.
};

/**
 * @brief Professor / instructor with teaching capabilities.
 *
 * The canTeach vectors store subject ids that this professor can teach
 * as course, seminar or lab, respectively.
 */
struct Professor {
    int id; ///< Unique professor identifier.
    std::string name; ///< Professor's name.
    std::vector<int> canTeachCourse; ///< Subject ids this professor can teach as courses.
    std::vector<int> canTeachSeminar; ///< Subject ids this professor can teach as seminars.
    std::vector<int> canTeachLab; ///< Subject ids this professor can teach as labs.
};

/**
 * @brief A student group that attends a fixed set of subjects.
 *
 * A group is treated as an atomic unit in scheduling: all students in the
 * group share the same timetable.
 */
struct Group {
    int id; ///< Unique group identifier.
    std::string name; ///< Human-readable group name/label.
    std::vector<int> subjects; ///< Subject ids taken by this group.
};

// Time grid: 5 days × 6 slots (2h each)
static constexpr int DAYS = 5;
static constexpr int SLOTS_PER_DAY = 6;

/**
 * @brief Types of teaching activities that can be scheduled.
 */
enum class ActivityType { COURSE, SEMINAR, LAB };

/**
 * @brief One concrete teaching activity to be placed in the timetable.
 *
 * Each activity corresponds to a single 2-hour session (course, seminar
 * or lab) for a given subject, professor and set of attending groups.
 */
struct Activity {
    int id; ///< Unique activity identifier (0..N-1 for indexing).
    int subjectId; ///< Subject being taught in this activity.
    ActivityType type; ///< Activity type (course / seminar / lab).
    int profId; ///< Id of the professor assigned to this activity.
    /**
     * For courses, groupIds contains all groups that attend together.
     * For seminars/labs, groupIds typically has a single group id.
     */
    std::vector<int> groupIds;
};

/**
 * @brief Complete problem instance describing the timetabling task.
 *
 * Contains all static data required by solvers: buildings, rooms, subjects,
 * professors, student groups, derived activities, and travel times between
 * buildings.
 */
struct ProblemInstance {
    std::vector<Building> buildings; ///< All buildings in the campus.
    std::vector<Room> rooms; ///< All rooms available for teaching.
    std::vector<Subject> subjects; ///< All subjects to be scheduled.
    std::vector<Professor> professors; ///< All professors involved in teaching.
    std::vector<Group> groups; ///< All student groups.
    std::vector<Activity> activities; ///< All activities to be placed in the timetable.

    /// travelTime[a][b] = minutes needed to move from building a to building b.
    std::vector<std::vector<int>> travelTime;
};
