#pragma once

#include <type_traits>

namespace cc
{
    using std::forward;

    using std::is_convertible;

    using std::is_array_v;
    using std::is_enum_v;
    using std::is_floating_point_v;
    using std::is_integral_v;
    using std::is_pointer_v;
    using std::is_same_v;
    using std::is_signed_v;
    using std::is_unsigned_v;

    using std::convertible_to;
    using std::enable_if;
    using std::remove_reference_t;
    using std::remove_pointer_t;
    using std::remove_const_t;

} // namespace cc
