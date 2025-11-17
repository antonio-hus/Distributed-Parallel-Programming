///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "formatting.hpp"
#include <iostream>
#include <array>
#include <algorithm>
#include <iomanip>

///////////////////////////
///       HELPERS       ///
///////////////////////////

/**
 * @brief Human-readable names for each teaching day.
 *
 * Indexed by the 0-based day index used in the timetable model.
 */
static const std::array<std::string, DAYS> kDayNames = {
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"
};

/**
 * @brief Human-readable time ranges for each slot in a day.
 *
 * Indexed by the 0-based slot index used in the timetable model.
 */
static const std::array<std::string, SLOTS_PER_DAY> kSlotRanges = {
        "08:00-10:00",
        "10:00-12:00",
        "12:00-14:00",
        "14:00-16:00",
        "16:00-18:00",
        "18:00-20:00"
};

/**
 * @brief Lightweight view of a single occupied slot for a specific group.
 *
 * Holds resolved pointers to activity, subject, professor and room to simplify
 * sorting and rendering when printing schedules.
 */
struct GroupSlotView {
    int day; ///< Day index of the activity.
    int slot; ///< Slot index within the day.
    const Activity* activity; ///< Associated activity (non-owning).
    const Subject* subject; ///< Subject taught in this slot (may be null).
    const Professor* prof; ///< Professor teaching (may be null).
    const Room* room; ///< Room where the activity takes place (may be null).
};

/**
 * @brief Convert an ActivityType enum to a human-readable label.
 */
static std::string formatType(ActivityType t) {
    switch (t) {
        case ActivityType::COURSE:  return "Course";
        case ActivityType::SEMINAR: return "Seminar";
        case ActivityType::LAB:     return "Lab";
    }
    return "Unknown";
}

/**
 * @brief Print the header row for a per-day schedule table.
 *
 * Uses fixed-width columns to align time, subject, type, professor and room.
 */
static void printDayTableHeader() {
    std::cout << "    "
              << std::left << std::setw(11) << "Time"
              << " | " << std::left << std::setw(12) << "Subject"
              << " | " << std::left << std::setw(8)  << "Type"
              << " | " << std::left << std::setw(12) << "Professor"
              << " | " << std::left << std::setw(8)  << "Room"
              << "\n";

    // Underline with a matching ASCII separator line.
    std::cout << "    "
              << std::string(11, '-')
              << "-+-" << std::string(12, '-')
              << "-+-" << std::string(8, '-')
              << "-+-" << std::string(12, '-')
              << "-+-" << std::string(8, '-')
              << "\n";
}

/**
 * @brief Print pretty, per-group schedules for a solved timetable.
 *
 * For each group, collects all activities attended by that group, resolves their
 * placements, then prints them grouped by day and ordered by time, with a small
 * table for each day showing time, subject, type, professor and room.
 */
void printGroupSchedules(const ProblemInstance& inst, const TimetableSolution& sol) {
    // Build a quick lookup from activity id to its placement in the final solution.
    std::vector<const Placement*> placementByAct(inst.activities.size(), nullptr);
    for (const Placement& p : sol.placements) {
        if (p.activityId >= 0 && p.activityId < (int)placementByAct.size()) {
            placementByAct[p.activityId] = &p;
        }
    }

    // Render schedules for each teaching group.
    for (const Group& g : inst.groups) {
        std::cout << "----------------------------------------\n";
        std::cout << "Schedule for " << g.name << ":\n";

        // Collect all occupied slots (GroupSlotView) for this group.
        std::vector<GroupSlotView> slots;

        for (const Activity& act : inst.activities) {
            // Check whether this group attends the activity.
            bool attends = false;
            for (int gid : act.groupIds) {
                if (gid == g.id) {
                    attends = true;
                    break;
                }
            }
            if (!attends) continue;

            // Find placement for this activity in the solution.
            if (act.id < 0 || act.id >= (int)placementByAct.size()) continue;
            const Placement* p = placementByAct[act.id];
            if (!p) continue;

            // Resolve referenced subject, professor and room.
            const Subject* subj = nullptr;
            if (act.subjectId >= 0 && act.subjectId < (int)inst.subjects.size()) {
                subj = &inst.subjects[act.subjectId];
            }

            const Professor* prof = nullptr;
            for (const Professor& pr : inst.professors) {
                if (pr.id == act.profId) {
                    prof = &pr;
                    break;
                }
            }

            const Room* room = nullptr;
            if (p->roomIndex >= 0 && p->roomIndex < (int)inst.rooms.size()) {
                room = &inst.rooms[p->roomIndex];
            }

            GroupSlotView view;
            view.day = p->day;
            view.slot = p->slot;
            view.activity = &act;
            view.subject = subj;
            view.prof = prof;
            view.room = room;
            slots.push_back(view);
        }

        // Sort all of this group's slots by day, then by time slot.
        std::sort(slots.begin(), slots.end(),
                  [](const GroupSlotView& a, const GroupSlotView& b) {
                      if (a.day != b.day) return a.day < b.day;
                      return a.slot < b.slot;
                  });

        if (slots.empty()) {
            std::cout << "  (no activities)\n";
            continue;
        }

        int currentDay = -1;
        // Walk through all slots, printing a small table per day.
        for (const GroupSlotView& s : slots) {
            // When the day changes, print a new day header and table header.
            if (s.day != currentDay) {
                currentDay = s.day;
                std::cout << "\n  " << kDayNames[s.day] << ":\n";
                printDayTableHeader();
            }

            // Fallback names make incomplete data obvious in the printed schedule.
            std::string subjName = s.subject ? s.subject->name : "UnknownSubject";
            std::string profName = s.prof ? s.prof->name : "UnknownProf";
            std::string roomName = s.room ? s.room->name : "UnknownRoom";
            std::string timeRange = (s.slot >= 0 && s.slot < (int)kSlotRanges.size()) ? kSlotRanges[s.slot] : "UnknownTime";
            std::string typeStr = formatType(s.activity->type);

            std::cout << "    "
                      << std::left << std::setw(11) << timeRange
                      << " | " << std::left << std::setw(12) << subjName
                      << " | " << std::left << std::setw(8)  << typeStr
                      << " | " << std::left << std::setw(12) << profName
                      << " | " << std::left << std::setw(8)  << roomName
                      << "\n";
        }

        std::cout << "\n";
    }
}
