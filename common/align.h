#pragma once

#include <common/align_val_t.h>

namespace cc
{
    template<typename Type>
    Type align_up(Type, align_val_t);

    template<typename Type>
    Type align_down(Type, align_val_t);
} // namespace cc

#include <common/align.inl>
