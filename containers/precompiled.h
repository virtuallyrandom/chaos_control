#pragma once

#pragma warning(disable:4324) // structure was padded due to alignment specifier
#pragma warning(disable:4514) // unreferenced inline function has been removed
#pragma warning(disable:4820) // 'n' bytes padding added after data member 'm'
#pragma warning(disable:5045) // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified

#include <common/atomic.h>
#include <common/compiler.h>
#include <common/concurrency.h>
#include <common/math.h>
#include <common/type_traits.h>
#include <common/types.h>

#include <unordered_map>
