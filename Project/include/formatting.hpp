#pragma once

///////////////////////////
///       IMPORTS       ///
///////////////////////////
#include "model.hpp"
#include "constraints.hpp"
#include "solver_base.hpp"


///////////////////////////
///       HELPERS       ///
///////////////////////////
void printGroupSchedules(const ProblemInstance& inst, const TimetableSolution& sol);
