#pragma once

#pragma warning(push, 0)

#pragma push_macro("ONLY_MSPACES")
#define ONLY_MSPACES 1

#include "malloc-2.8.6.h"

#undef ONLY_MSPACES
#pragma pop_macro("ONLY_MSPACES")

#pragma warning(pop)
