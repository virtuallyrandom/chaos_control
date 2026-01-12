#pragma once

#include <common/string.h>

namespace cc
{
    class test
    {
    public:
        test();
        virtual ~test() = default;

        virtual const char* name() const = 0;

        // return a string containing errors or empty if none occurred.
        virtual string operator()() = 0;

        test* next() const { return m_next; }

    private:
        test* m_next{};
    };

    extern test* gFirstTest;
} // namespace cc
