#include "test.h"

namespace cc
{
    extern test* gFirstTest{};

    test::test()
    {
        m_next = gFirstTest;
        gFirstTest = this;
    }
} // namespace cc
