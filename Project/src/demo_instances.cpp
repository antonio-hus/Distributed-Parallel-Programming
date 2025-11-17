#pragma once
#include <vector>
#include <string>
#include "model.hpp"
#include "demo_instances.hpp"

///////////////////////////
///     DEMO: SMALL     ///
///////////////////////////
static ProblemInstance makeDemoSmall() {
    ProblemInstance inst;

    inst.buildings.push_back({0, "A"});
    inst.buildings.push_back({1, "B"});

    inst.travelTime = {
            {0, 5},
            {5, 0}
    };

    inst.rooms.push_back({0, 0, "A101", 60, Room::Type::COURSE});
    inst.rooms.push_back({1, 0, "A201", 30, Room::Type::SEMINAR});
    inst.rooms.push_back({2, 1, "B301", 20, Room::Type::LAB});

    inst.subjects.push_back({0, "Math", 1, 1, 0});
    inst.subjects.push_back({1, "Programming", 1, 0, 1});

    Professor alice;
    alice.id = 0;
    alice.name = "Prof. Alice";
    alice.canTeachCourse = {0};
    alice.canTeachSeminar = {0};
    alice.canTeachLab = {};

    Professor bob;
    bob.id = 1;
    bob.name = "Prof. Bob";
    bob.canTeachCourse = {1};
    bob.canTeachSeminar = {};
    bob.canTeachLab = {1};

    inst.professors.push_back(alice);
    inst.professors.push_back(bob);

    Group g0{0, "Group 1", {0, 1}};
    Group g1{1, "Group 2", {0, 1}};
    inst.groups.push_back(g0);
    inst.groups.push_back(g1);

    int nextActivityId = 0;

    // Courses (2)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1};
        inst.activities.push_back(a);
    }
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1};
        inst.activities.push_back(a);
    }

    // Seminars (2)
    for (int g = 0; g < 2; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::SEMINAR;
        a.profId = 0;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }

    // Labs (2)
    for (int g = 0; g < 2; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }

    // 6 activities (small toy instance)
    return inst;
}

///////////////////////////
///    DEMO: MEDIUM     ///
///////////////////////////
static ProblemInstance makeDemoMedium() {
    ProblemInstance inst;

    inst.buildings.push_back({0, "A"});
    inst.buildings.push_back({1, "B"});

    inst.travelTime = {
            {0, 5},
            {5, 0}
    };

    inst.rooms.push_back({0, 0, "A101", 100, Room::Type::COURSE});
    inst.rooms.push_back({1, 0, "A201", 40, Room::Type::SEMINAR});
    inst.rooms.push_back({2, 1, "B301", 30, Room::Type::LAB});

    inst.subjects.push_back({0, "Math",        2, 1, 0});
    inst.subjects.push_back({1, "Programming", 1, 0, 1});
    inst.subjects.push_back({2, "Physics",     1, 0, 1});

    Professor alice;
    alice.id = 0;
    alice.name = "Prof. Alice";
    alice.canTeachCourse = {0, 1};
    alice.canTeachSeminar = {0};
    alice.canTeachLab = {};

    Professor bob;
    bob.id = 1;
    bob.name = "Prof. Bob";
    bob.canTeachCourse = {1, 2};
    bob.canTeachSeminar = {};
    bob.canTeachLab = {1, 2};

    inst.professors.push_back(alice);
    inst.professors.push_back(bob);

    for (int g = 0; g < 3; ++g) {
        Group group;
        group.id = g;
        group.name = "Group " + std::to_string(g + 1);
        group.subjects = {0, 1, 2};
        inst.groups.push_back(group);
    }

    int nextActivityId = 0;

    // Courses: Math (2x), Programming (1x), Physics (1x) => 4
    for (int k = 0; k < 2; ++k) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }

    // Seminars: Math, 1 per group => 3
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::SEMINAR;
        a.profId = 0;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }

    // Labs: Programming + Physics, 1 per group each => 3 + 3 = 6
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }

    // Total activities: 4 + 3 + 6 = 13
    return inst;
}

///////////////////////////
///     DEMO: LARGE     ///
///////////////////////////
static ProblemInstance makeDemoLarge() {
    ProblemInstance inst;

    // Buildings
    inst.buildings.push_back({0, "A"});
    inst.buildings.push_back({1, "B"});
    inst.buildings.push_back({2, "C"});

    // Travel times
    inst.travelTime = {
            {0, 5, 8},
            {5, 0, 6},
            {8, 6, 0}
    };

    // Rooms
    inst.rooms.push_back({0, 0, "A101", 120, Room::Type::COURSE});
    inst.rooms.push_back({1, 0, "A201", 40,  Room::Type::SEMINAR});
    inst.rooms.push_back({2, 0, "A202", 30,  Room::Type::SEMINAR});
    inst.rooms.push_back({3, 1, "B301", 30,  Room::Type::LAB});
    inst.rooms.push_back({4, 1, "B302", 25,  Room::Type::LAB});
    inst.rooms.push_back({5, 2, "C101", 80,  Room::Type::COURSE});
    inst.rooms.push_back({6, 2, "C201", 35,  Room::Type::SEMINAR});

    // Subjects: 4 subjects, tuned to ~30 activities
    // id, name, courseSlots, seminarSlots, labSlots
    inst.subjects.push_back({0, "Math",        2, 1, 0});
    inst.subjects.push_back({1, "Programming", 1, 0, 2});
    inst.subjects.push_back({2, "Physics",     1, 1, 1});
    inst.subjects.push_back({3, "Databases",   1, 1, 1});

    // Professors
    Professor alice;
    alice.id = 0;
    alice.name = "Prof. Alice";
    alice.canTeachCourse = {0, 2}; // Math, Physics
    alice.canTeachSeminar = {0, 2, 3}; // Math, Physics, Databases
    alice.canTeachLab = {};

    Professor bob;
    bob.id = 1;
    bob.name = "Prof. Bob";
    bob.canTeachCourse = {1, 3}; // Programming, Databases
    bob.canTeachSeminar = {};
    bob.canTeachLab = {1, 2, 3}; // Programming/Physics/DB

    Professor carol;
    carol.id = 2;
    carol.name = "Prof. Carol";
    carol.canTeachCourse = {2, 3}; // Physics, Databases
    carol.canTeachSeminar = {0, 2, 3};
    carol.canTeachLab = {1, 2}; // Prog, Physics

    inst.professors.push_back(alice);
    inst.professors.push_back(bob);
    inst.professors.push_back(carol);

    // Groups: 3 groups, all subjects
    for (int g = 0; g < 3; ++g) {
        Group group;
        group.id = g;
        group.name = "Group " + std::to_string(g + 1);
        group.subjects = {0, 1, 2, 3};
        inst.groups.push_back(group);
    }

    int nextActivityId = 0;

    // Courses: 2 (Math) + 1 (Prog) + 1 (Phys) + 1 (DB) = 5
    // Math (Alice)
    for (int k = 0; k < 2; ++k) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }
    // Programming (Bob)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }
    // Physics (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }
    // Databases (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2};
        inst.activities.push_back(a);
    }

    // Seminars:
    // Math: 1 per group => 3
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::SEMINAR;
        a.profId = (g == 0 ? 0 : 2); // mix Alice/Carol
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Physics: 1 per group => 3
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Databases: 1 per group => 3
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }

    // Labs:
    // Programming: 2 per group => 3*2 = 6
    for (int g = 0; g < 3; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 1;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2); // Bob and Carol
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Physics: 1 per group => 3
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Databases: 1 per group => 3
    for (int g = 0; g < 3; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }

    // Count: courses 5 + seminars (3+3+3=9) + labs (6+3+3=12) = 26
    // To reach exactly 30, add 4 more labs for Programming (e.g., extra practice):
    for (int extra = 0; extra < 4; ++extra) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::LAB;
        a.profId = (extra % 2 == 0 ? 1 : 2);
        a.groupIds = {extra % 3};
        inst.activities.push_back(a);
    }

    // Total = 26 + 4 = 30

    return inst;
}

///////////////////////////
///     DEMO: XL        ///
///////////////////////////
static ProblemInstance makeDemoXL() {
    ProblemInstance inst;

    // Buildings
    inst.buildings.push_back({0, "A"});
    inst.buildings.push_back({1, "B"});
    inst.buildings.push_back({2, "C"});

    // Travel times
    inst.travelTime = {
            {0, 4, 7},
            {4, 0, 6},
            {7, 6, 0}
    };

    // Rooms
    inst.rooms.push_back({0, 0, "A101", 150, Room::Type::COURSE});
    inst.rooms.push_back({1, 0, "A201", 50,  Room::Type::SEMINAR});
    inst.rooms.push_back({2, 0, "A202", 40,  Room::Type::SEMINAR});
    inst.rooms.push_back({3, 1, "B301", 30,  Room::Type::LAB});
    inst.rooms.push_back({4, 1, "B302", 30,  Room::Type::LAB});
    inst.rooms.push_back({5, 1, "B303", 25,  Room::Type::LAB});
    inst.rooms.push_back({6, 2, "C101", 100, Room::Type::COURSE});
    inst.rooms.push_back({7, 2, "C201", 40,  Room::Type::SEMINAR});

    // Subjects: 5 subjects
    // id, name, courseSlots, seminarSlots, labSlots
    inst.subjects.push_back({0, "Math",        2, 1, 0});
    inst.subjects.push_back({1, "Programming", 1, 0, 2});
    inst.subjects.push_back({2, "Physics",     1, 1, 2});
    inst.subjects.push_back({3, "Databases",   1, 1, 1});
    inst.subjects.push_back({4, "Algorithms",  1, 1, 0});

    // Professors
    Professor alice;
    alice.id = 0;
    alice.name = "Prof. Alice";
    alice.canTeachCourse = {0, 4}; // Math, Algorithms
    alice.canTeachSeminar = {0, 2, 4}; // Math, Physics, Algorithms
    alice.canTeachLab = {};

    Professor bob;
    bob.id = 1;
    bob.name = "Prof. Bob";
    bob.canTeachCourse = {1, 3}; // Programming, Databases
    bob.canTeachSeminar = {3}; // Databases seminars
    bob.canTeachLab = {1, 2, 3}; // Programming/Physics/DB labs

    Professor carol;
    carol.id = 2;
    carol.name = "Prof. Carol";
    carol.canTeachCourse = {2, 3, 4}; // Physics, Databases, Algorithms
    carol.canTeachSeminar = {0, 2, 3, 4};
    carol.canTeachLab = {1, 2}; // Programming & Physics labs

    inst.professors.push_back(alice);
    inst.professors.push_back(bob);
    inst.professors.push_back(carol);

    // Groups: 4 groups, all 5 subjects
    for (int g = 0; g < 4; ++g) {
        Group group;
        group.id = g;
        group.name = "Group " + std::to_string(g + 1);
        group.subjects = {0, 1, 2, 3, 4};
        inst.groups.push_back(group);
    }

    int nextActivityId = 0;

    // Courses:
    // Math: 2 (Alice)
    for (int k = 0; k < 2; ++k) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2, 3};
        inst.activities.push_back(a);
    }
    // Programming: 1 (Bob)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2, 3};
        inst.activities.push_back(a);
    }
    // Physics: 1 (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2, 3};
        inst.activities.push_back(a);
    }
    // Databases: 1 (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2, 3};
        inst.activities.push_back(a);
    }
    // Algorithms: 1 (Alice)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 4;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2, 3};
        inst.activities.push_back(a);
    }
    // Courses total: 2 + 1 + 1 + 1 + 1 = 6

    // Seminars:
    // Math: 1 per group => 4
    for (int g = 0; g < 4; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::SEMINAR;
        a.profId = (g < 2 ? 0 : 2); // split Alice/Carol
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Physics: 1 per group => 4
    for (int g = 0; g < 4; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Databases: 1 per group => 4
    for (int g = 0; g < 4; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Algorithms: 1 per group => 4
    for (int g = 0; g < 4; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 4;
        a.type = ActivityType::SEMINAR;
        a.profId = 0;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Seminars total: 4 + 4 + 4 + 4 = 16

    // Labs:
    // Programming: 2 per group => 8
    for (int g = 0; g < 4; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 1;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Physics: 2 per group => 8
    for (int g = 0; g < 4; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 2;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Databases: 1 per group => 4
    for (int g = 0; g < 4; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Labs total: 8 + 8 + 4 = 20

    // Current total: courses 6 + seminars 16 + labs 20 = 42
    // Add 3 more labs (extra Programming practice) to reach 45
    for (int extra = 0; extra < 3; ++extra) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::LAB;
        a.profId = (extra % 2 == 0 ? 1 : 2);
        a.groupIds = {extra % 4};
        inst.activities.push_back(a);
    }

    // Total = 42 + 3 = 45

    return inst;
}

///////////////////////////
///     DEMO: XXL       ///
///////////////////////////
static ProblemInstance makeDemoXXL() {
    ProblemInstance inst;

    // Buildings
    inst.buildings.push_back({0, "A"});
    inst.buildings.push_back({1, "B"});
    inst.buildings.push_back({2, "C"});

    // Travel times
    inst.travelTime = {
            {0, 4, 8},
            {4, 0, 6},
            {8, 6, 0}
    };

    // Rooms: add one more course and lab room for density
    inst.rooms.push_back({0, 0, "A101", 160, Room::Type::COURSE});
    inst.rooms.push_back({1, 0, "A201", 60,  Room::Type::SEMINAR});
    inst.rooms.push_back({2, 0, "A202", 50,  Room::Type::SEMINAR});
    inst.rooms.push_back({3, 1, "B301", 35,  Room::Type::LAB});
    inst.rooms.push_back({4, 1, "B302", 35,  Room::Type::LAB});
    inst.rooms.push_back({5, 1, "B303", 30,  Room::Type::LAB});
    inst.rooms.push_back({6, 2, "C101", 120, Room::Type::COURSE});
    inst.rooms.push_back({7, 2, "C201", 50,  Room::Type::SEMINAR});
    inst.rooms.push_back({8, 2, "C202", 40,  Room::Type::SEMINAR});

    // Subjects: 6 subjects, slightly heavier requirements
    // id, name, courseSlots, seminarSlots, labSlots
    inst.subjects.push_back({0, "Math",         2, 1, 0});
    inst.subjects.push_back({1, "Programming",  2, 0, 2});
    inst.subjects.push_back({2, "Physics",      1, 1, 2});
    inst.subjects.push_back({3, "Databases",    1, 1, 1});
    inst.subjects.push_back({4, "Algorithms",   1, 1, 0});
    inst.subjects.push_back({5, "OperatingSys", 1, 1, 2});

    // Professors
    Professor alice;
    alice.id = 0;
    alice.name = "Prof. Alice";
    alice.canTeachCourse = {0, 4}; // Math, Algorithms
    alice.canTeachSeminar = {0, 2, 4}; // Math, Physics, Algorithms
    alice.canTeachLab = {};

    Professor bob;
    bob.id = 1;
    bob.name = "Prof. Bob";
    bob.canTeachCourse = {1, 3, 5}; // Programming, Databases, OS
    bob.canTeachSeminar = {3, 5};
    bob.canTeachLab = {1, 2, 3, 5}; // Programming/Physics/DB/OS

    Professor carol;
    carol.id = 2;
    carol.name = "Prof. Carol";
    carol.canTeachCourse = {2, 3, 4, 5}; // Physics, Databases, Algorithms, OS
    carol.canTeachSeminar = {0, 2, 3, 4, 5};
    carol.canTeachLab = {1, 2, 5}; // Programming/Physics/OS

    inst.professors.push_back(alice);
    inst.professors.push_back(bob);
    inst.professors.push_back(carol);

    // Groups: 5 groups, all subjects
    for (int g = 0; g < 5; ++g) {
        Group group;
        group.id = g;
        group.name = "Group " + std::to_string(g + 1);
        group.subjects = {0, 1, 2, 3, 4, 5};
        inst.groups.push_back(group);
    }

    int nextActivityId = 0;

    // Courses:
    // Math: 2 (Alice)
    for (int k = 0; k < 2; ++k) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2, 3, 4};
        inst.activities.push_back(a);
    }
    // Programming: 2 (Bob)
    for (int k = 0; k < 2; ++k) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2, 3, 4};
        inst.activities.push_back(a);
    }
    // Physics: 1 (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2, 3, 4};
        inst.activities.push_back(a);
    }
    // Databases: 1 (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2, 3, 4};
        inst.activities.push_back(a);
    }
    // Algorithms: 1 (Alice)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 4;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2, 3, 4};
        inst.activities.push_back(a);
    }
    // OS: 1 (Bob)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 5;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2, 3, 4};
        inst.activities.push_back(a);
    }
    // Courses total: 2 + 2 + 1 + 1 + 1 + 1 = 8

    // Seminars:
    // Math: 1 per group => 5
    for (int g = 0; g < 5; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::SEMINAR;
        a.profId = (g < 3 ? 0 : 2);
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Physics: 1 per group => 5
    for (int g = 0; g < 5; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Databases: 1 per group => 5
    for (int g = 0; g < 5; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Algorithms: 1 per group => 5
    for (int g = 0; g < 5; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 4;
        a.type = ActivityType::SEMINAR;
        a.profId = 0;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // OS: 1 per group => 5
    for (int g = 0; g < 5; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 5;
        a.type = ActivityType::SEMINAR;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Seminars total: 5 * 5 = 25

    // Labs:
    // Programming: 2 per group => 10
    for (int g = 0; g < 5; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 1;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Physics: 2 per group => 10
    for (int g = 0; g < 5; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 2;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // OS: 2 per group => 10
    for (int g = 0; g < 5; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 5;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Databases: 1 per group => 5
    for (int g = 0; g < 5; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::LAB;
        a.profId = 1;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Labs total: 10 + 10 + 10 + 5 = 35

    return inst;
}

///////////////////////////
///    DEMO: XXXL       ///
///////////////////////////
// Target: 90 activities
static ProblemInstance makeDemoXXXL() {
    ProblemInstance inst;

    // Buildings
    inst.buildings.push_back({0, "A"});
    inst.buildings.push_back({1, "B"});
    inst.buildings.push_back({2, "C"});
    inst.buildings.push_back({3, "D"});

    // Travel times (roughly realistic)
    inst.travelTime = {
            {0, 4, 7, 10},
            {4, 0, 6, 9},
            {7, 6, 0, 5},
            {10, 9, 5, 0}
    };

    // Rooms: more variety, but not too many labs
    inst.rooms.push_back({0, 0, "A101", 180, Room::Type::COURSE});
    inst.rooms.push_back({1, 0, "A201", 60,  Room::Type::SEMINAR});
    inst.rooms.push_back({2, 0, "A202", 60,  Room::Type::SEMINAR});
    inst.rooms.push_back({3, 1, "B301", 40,  Room::Type::LAB});
    inst.rooms.push_back({4, 1, "B302", 40,  Room::Type::LAB});
    inst.rooms.push_back({5, 1, "B303", 35,  Room::Type::LAB});
    inst.rooms.push_back({6, 2, "C101", 150, Room::Type::COURSE});
    inst.rooms.push_back({7, 2, "C201", 50,  Room::Type::SEMINAR});
    inst.rooms.push_back({8, 3, "D101", 120, Room::Type::COURSE});
    inst.rooms.push_back({9, 3, "D201", 45,  Room::Type::SEMINAR});

    // Subjects: 6 subjects including project-heavy one
    // id, name, courseSlots, seminarSlots, labSlots
    inst.subjects.push_back({0, "Math",         2, 1, 0});
    inst.subjects.push_back({1, "Programming",  2, 0, 3});
    inst.subjects.push_back({2, "Physics",      2, 1, 2});
    inst.subjects.push_back({3, "Databases",    1, 1, 2});
    inst.subjects.push_back({4, "Algorithms",   1, 1, 0});
    inst.subjects.push_back({5, "Projects",     1, 1, 2});

    // Professors
    Professor alice;
    alice.id = 0;
    alice.name = "Prof. Alice";
    alice.canTeachCourse = {0, 4}; // Math, Algorithms
    alice.canTeachSeminar = {0, 2, 4}; // Math, Physics, Algorithms
    alice.canTeachLab = {};

    Professor bob;
    bob.id = 1;
    bob.name = "Prof. Bob";
    bob.canTeachCourse = {1, 3, 5}; // Programming, DB, Projects
    bob.canTeachSeminar = {3, 5};
    bob.canTeachLab = {1, 2, 3, 5}; // Programming/Physics/DB/Projects

    Professor carol;
    carol.id = 2;
    carol.name = "Prof. Carol";
    carol.canTeachCourse = {2, 3, 4, 5}; // Physics, DB, Alg, Projects
    carol.canTeachSeminar = {0, 2, 3, 4, 5};
    carol.canTeachLab = {1, 2, 5};

    Professor dave;
    dave.id = 3;
    dave.name = "Prof. Dave";
    dave.canTeachCourse = {1, 2, 5}; // Programming, Physics, Projects
    dave.canTeachSeminar = {1, 2, 5};
    dave.canTeachLab = {1, 2, 3, 5};

    inst.professors.push_back(alice);
    inst.professors.push_back(bob);
    inst.professors.push_back(carol);
    inst.professors.push_back(dave);

    // Groups: 6 groups, all subjects
    for (int g = 0; g < 6; ++g) {
        Group group;
        group.id = g;
        group.name = "Group " + std::to_string(g + 1);
        group.subjects = {0, 1, 2, 3, 4, 5};
        inst.groups.push_back(group);
    }

    int nextActivityId = 0;

    // Courses:
    // Math: 2 slots (Alice)
    for (int k = 0; k < 2; ++k) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    // Programming: 2 slots (Bob/Dave)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::COURSE;
        a.profId = 3;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    // Physics: 2 slots (Carol/Dave)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::COURSE;
        a.profId = 3;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    // Databases: 1 slot (Carol)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::COURSE;
        a.profId = 2;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    // Algorithms: 1 slot (Alice)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 4;
        a.type = ActivityType::COURSE;
        a.profId = 0;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    // Projects: 1 slot (Bob)
    {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 5;
        a.type = ActivityType::COURSE;
        a.profId = 1;
        a.groupIds = {0, 1, 2, 3, 4, 5};
        inst.activities.push_back(a);
    }
    // Courses total: 2 + 2 + 2 + 1 + 1 + 1 = 9

    // Seminars:
    // Math: 1 per group => 6
    for (int g = 0; g < 6; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 0;
        a.type = ActivityType::SEMINAR;
        a.profId = (g < 3 ? 0 : 2);
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Programming: 1 per group => 6
    for (int g = 0; g < 6; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 1;
        a.type = ActivityType::SEMINAR;
        a.profId = (g < 3 ? 1 : 3);
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Physics: 1 per group => 6
    for (int g = 0; g < 6; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 2;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Databases: 1 per group => 6
    for (int g = 0; g < 6; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 3;
        a.type = ActivityType::SEMINAR;
        a.profId = 2;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Algorithms: 1 per group => 6
    for (int g = 0; g < 6; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 4;
        a.type = ActivityType::SEMINAR;
        a.profId = 0;
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Projects: 1 per group => 6
    for (int g = 0; g < 6; ++g) {
        Activity a;
        a.id = nextActivityId++;
        a.subjectId = 5;
        a.type = ActivityType::SEMINAR;
        a.profId = (g < 3 ? 1 : 3);
        a.groupIds = {g};
        inst.activities.push_back(a);
    }
    // Seminars total: 6 * 6 = 36

    // Labs:
    // Programming: 3 per group => 18
    for (int g = 0; g < 6; ++g) {
        for (int k = 0; k < 3; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 1;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : (k == 1 ? 2 : 3));
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Physics: 2 per group => 12 (total 30)
    for (int g = 0; g < 6; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 2;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 2 : 3);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Databases: 2 per group => 12 (total 42)
    for (int g = 0; g < 6; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 3;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 3);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }
    // Projects: 2 per group => 12 (total 54)
    for (int g = 0; g < 6; ++g) {
        for (int k = 0; k < 2; ++k) {
            Activity a;
            a.id = nextActivityId++;
            a.subjectId = 5;
            a.type = ActivityType::LAB;
            a.profId = (k == 0 ? 1 : 2);
            a.groupIds = {g};
            inst.activities.push_back(a);
        }
    }

    return inst;
}

///////////////////////////
///    DEMO FACTORY     ///
///////////////////////////
ProblemInstance makeDemoInstance(DemoSize size) {
    switch (size) {
        case DemoSize::XS: return makeDemoSmall();
        case DemoSize::S: return makeDemoSmall();
        case DemoSize::M: return makeDemoMedium();
        case DemoSize::L: return makeDemoLarge();
        case DemoSize::XL: return makeDemoXL();
        case DemoSize::XXL: return makeDemoXXL();
        case DemoSize::XXXL: return makeDemoXXXL();
    }
    return makeDemoSmall();
}
