#pragma once

#include <common/string.h>
#include <script/rule.h>

namespace cc::script
{
    bool loadLanguage(string const& json, rules&);

    // convert a stream of text into a stream of tokens according to lexical rules.
    // rules are applied as "whichever processes the most wins" followed by priority.
    // if a string contains x0123456789abcdefghi, it can be processed as both hex
    // or ident, but ident will win as it will process more characters.
    bool tokenize(string const& script, rules const&, tokens&);
} // namespace cc::script
