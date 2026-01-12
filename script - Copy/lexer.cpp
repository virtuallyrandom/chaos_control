#include <script/lexer.h>

#include <common/file.h>
#include <common/math.h>
#include <common/memory.h>
#include <common/utility.h>
#include <containers/unordered_map.h>

#pragma warning(push,1)
#include <external/json/reader.h>
#pragma warning(pop)

namespace
{
    struct template_definition
    {
        cc::string name;
        cc::string format;
    };

    struct constraint_definition
    {
        cc::string name;
        cc::string description;
        cc::string format;
        int32_t priority;
        bool tokenize;
        uint8_t pad[3]{};
    };

    static cc::string getString(Json::Value const& in, char const* const name, cc::string const& defaultValue)
    {
        const Json::Value& v = in[name];
        if (!v.isString())
            return defaultValue;
        return v.asString();
    }

    static int32_t getInt32(Json::Value const& in, char const* const name, int32_t const defaultValue)
    {
        const Json::Value& v = in[name];
        if (!v.isInt())
            return defaultValue;
        return v.asInt();
    }

    static bool getBool(Json::Value const& in, char const* const name, bool const defaultValue)
    {
        const Json::Value& v = in[name];
        if (!v.isBool())
            return defaultValue;
        return v.asBool();
    }

    static cc::script::rule::rate_of consumeRate(cc::string const& str, size_t& pos)
    {
        // is there a rate specifier?
        if (pos + 1 >= str.size())
            return cc::script::rule::rate_of::kOnce;

        if (str[pos + 1] == '?')
        {
            pos++;
            return cc::script::rule::rate_of::kZeroOrOne;
        }
        else if (str[pos + 1] == '*')
        {
            pos++;
            return cc::script::rule::rate_of::kZeroOrMore;
        }
        else if (str[pos + 1] == '+')
        {
            pos++;
            return cc::script::rule::rate_of::kOneOrMore;
        }

        return cc::script::rule::rate_of::kOnce;
    }

    static void generateRule(constraint_definition const& cons, cc::script::rules& rules)
    {
        // copy the format and replace all template occurrences
        cc::string str = cons.format;

        cc::script::rule r;

        r.name = cons.name;
        r.description = cons.description;
        r.priority = cons.priority;
        r.tokenize = cons.tokenize;

        // turn the string into a set of rules
        for (size_t offset = 0; offset < str.size(); offset++)
        {
            // escapement outside of a block, so it's an automatic once
            if (str[offset] == '\\')
            {
                if (++offset > str.size())
                    break;

                cc::script::rule::block b;
                b.set.emplace_back(str[offset]);
                b.rate = consumeRate(str, offset);
                r.blocks.emplace_back(b);
            }

            // add the set to a new block
            else if (str[offset] == '[')
            {
                r.blocks.emplace_back();
                cc::script::rule::block& b = r.blocks.back();

                for (size_t pos = offset + 1; pos < str.size(); pos++)
                {
                    if (str[pos] == '\\')
                    {
                        if (++pos > str.size())
                            break;

                        b.set.emplace_back(str[pos]);
                    }

                    else if (str[pos] == ']')
                    {
                        offset = pos;
                        b.rate = consumeRate(str, offset);
                        break;
                    }

                    else
                        b.set.emplace_back(str[pos]);
                }
            }

            // non-special character, must be format spec
            else
            {
                cc::script::rule::block b;
                b.set.emplace_back(str[offset]);
                b.rate = consumeRate(str, offset);
                r.blocks.emplace_back(b);
            }
        }

        rules.emplace(cons.name, r);
    }

    static size_t processRule(cc::script::rule const& r, cc::script::rules const& rules, char const* const start);

    // returns 0 if the constraint doesn't match, >= 1 if it does match, SIZE_MAX on error
    static size_t processConstraint(cc::script::constraint const& cns, cc::script::rules const& rules, char const* const start)
    {
        if (cns.type() == cc::script::constraint_type::kAsChar)
            return cns.asChar() == *start ? 1u : 0;

        else if (cns.type() == cc::script::constraint_type::kAsConstraint)
        {
            cc::script::rules::const_iterator const iter = rules.find(cns.asString());
            assert(iter != rules.end()); // constraint string not found in rules
            size_t const rv = processRule(iter->second, rules, start);
            if (rv == SIZE_MAX)
                return 0;
            return rv;
        }

        assert(false); // unknown
        return SIZE_MAX;
    }

    // returns SIZE_MAX if the block cannot be satisfied, the number of consumed characters otherwise
    static size_t processBlock(cc::script::rule::block const& block, cc::script::rules const& rules, char const* const start)
    {
        size_t offset = 0;

        for (cc::script::constraint const& cns : block.set)
        {
            // find the longest constraint match in the set
            size_t const rv = processConstraint(cns, rules, start + offset);
            if (rv != SIZE_MAX)
                offset = cc::max(offset, rv);
        }

        switch (block.rate)
        {
            case cc::script::rule::rate_of::kOnce:
                if (offset == 0)
                    return SIZE_MAX;
                return offset;

            case cc::script::rule::rate_of::kOneOrMore:
            {
                if (offset == 0)
                    return SIZE_MAX;
                size_t const rv = processBlock(block, rules, start + offset);
                if (rv != SIZE_MAX)
                    offset += rv;
                return offset;
            }

            case cc::script::rule::rate_of::kZeroOrMore:
            {
                if (offset == 0)
                    return 0;

                size_t const rv = processBlock(block, rules, start + offset);
                if (rv != SIZE_MAX)
                    offset += rv;
                return offset;
            }

            case cc::script::rule::rate_of::kZeroOrOne:
                return offset;
        }

        return offset;
    }

    // returns SIZE_MAX if the rule cannot be satisfied, the number of consumed characters otherwise
    static size_t processRule(cc::script::rule const& r, cc::script::rules const& rules, char const* const start)
    {
        size_t offset = 0;

        // a rule is made of up blocks, each of which is mandatory.
        // a block is satisfied by a set of constraints and a repetition count.

        // 0?x[{base16}]+
        //
        // '0', 0 or 1
        // 'x', exactly 1
        // one or more from the '{base16}' template

        // process each block
        for (cc::script::rule::block const& b : r.blocks)
        {
            size_t const rv = processBlock(b, rules, start + offset);

            // failed to process the block, so just abort
            if (rv == SIZE_MAX)
            {
                if (offset == 0)
                    return SIZE_MAX;
                return offset;
            }

            offset += rv;
        }

        return offset;
    }
} // namespace [anonymous]

bool cc::script::loadLexRules(cc::string const& json, cc::script::rules& rules)
{
    vector<constraint_definition> consDef;

    try
    {
        Json::Reader reader;
        Json::Value root;

        if (!reader.parse(json, root))
            throw Json::Exception(reader.getFormattedErrorMessages());

        if (root.empty())
            return true;

        // load the constraints
        Json::Value::Members members = root.getMemberNames();

        consDef.reserve(members.size());

        for (Json::String name : members)
        {
            Json::Value const& def = root[name];
            string const& desc = getString(def, "description", "");
            string format = getString(def, "format", "");
            int32_t const priority = getInt32(def, "priority", 0ul);
            bool const tokenize = getBool(def, "tokenize", true);

            if (!tokenize)
                continue;

            // recursively substitute any {templates} with their json format
            size_t offset = 0;
            for(;;)
            {
                offset = format.find_first_of('{', offset);
                if (offset == format.npos)
                    break;

                size_t const end = format.find_first_of('}', offset);
                if (end == format.npos)
                    break;

                cc::string const subs = format.substr(offset + 1, end - offset - 1);
                Json::Value const& sref = root[subs];
                format.replace(offset, (end - offset) + 1, getString(sref, "format", ""));
            }

            consDef.push_back({ name, desc, format, priority, tokenize });
        }
    }
    catch (Json::Exception& e)
    {
        printf("Exception: %s\n", e.what());
        return false;
    }

    for (constraint_definition& cons : consDef)
        generateRule(cons, rules);

    return validate(rules);
}

bool cc::script::tokenize(cc::string const& script, cc::script::rules const& rules, cc::script::tokens& tokens)
{
    char const* cur = script.c_str();
    char const* const eof = script.c_str() + script.size();

    cc::string bestRule;

    while (cur < eof)
    {
        size_t bestLength = 0;
        int64_t bestPriority = 0;

        for (cc::pair<cc::string, cc::script::rule> const& r : rules)
        {
            // some rules don't tokenize at the top level (but do recursively) so save time and skip them here
            if (r.second.tokenize == false)
                continue;

            size_t const rv = processRule(r.second, rules, cur);
            if (rv == SIZE_MAX)
                continue;

            if (rv > bestLength)
            {
                bestRule = r.first;
                bestPriority = r.second.priority;
                bestLength = rv;
            }
            else if (rv == bestLength && (bestRule.empty() || r.second.priority > bestPriority))
            {
                bestRule = r.first;
                bestPriority = r.second.priority;
                bestLength = rv;
            }
        }

        assert(bestLength > 0); // couldn't find a rule for this token

        tokens.emplace_back(token{ bestRule, cur, cur + bestLength });

        cur += bestLength;
    }

    return false;
}
