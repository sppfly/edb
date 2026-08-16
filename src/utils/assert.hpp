#pragma once

// src/utils/assert.hpp
//
// Project-wide precondition/invariant assertion macro.
// Backed by the standard assert(); compiled out under NDEBUG.

#include <cassert>

#define EDB_ASSERT(condition) assert(condition)
