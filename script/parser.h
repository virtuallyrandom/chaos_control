#pragma once

#include <common/compiler.h>
#include <common/types.h>
#include <script/rule.h>

namespace cc::script
{
    struct grammar
    {
        string token;
        char const* start;
        char const* end;
    };
    using grammars = vector<grammar>;

    bool loadGrammar(string const& json, grammars&);
//    bool parse(cc::script::rules const&, cc::script::tokens const&, cc::script::commands&);
} // namespace cc::script
