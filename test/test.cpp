#include "test.h"

namespace cc
{
    extern test* g_first_test{};

    test::test()
    {
        m_next = g_first_test;
        g_first_test = this;
    }
} // namespace cc
