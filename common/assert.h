#pragma once

#include <cassert>

#define verify(expect, v) do { decltype(expect) const rv = (v); assert(rv == (expect)); } while(false)
