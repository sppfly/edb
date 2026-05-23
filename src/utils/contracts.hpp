#pragma once

// src/utils/contracts.hpp
//
// Contract annotation helpers.
//
// GCC 16 supports the experimental C++26 contract syntax under -fcontracts.
// Clang/clang-tidy does not parse that syntax yet, so contract annotations must
// disappear when the source is analyzed by Clang tooling.

#ifdef __clang__
#define EDB_PRE(condition)
#else
#define EDB_PRE(condition) pre(condition)
#endif
