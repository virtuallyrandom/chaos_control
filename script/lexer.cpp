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
#if 0
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
#endif // 0

    using KVMap = cc::unordered_map<cc::string, cc::string>;
    using KVPair = cc::pair<cc::string, cc::string>;

    static void injectTemplates(KVMap& targetMap, KVMap& tmplMap)
    {
        // recursively substitute any {templates} with their json format
        for (KVPair pair : targetMap)
        {
            cc::string& value = pair.second;
            bool modified = false;

            size_t offset = 0;
            for (;;)
            {
                offset = value.find_first_of('{', offset);
                if (offset == value.npos)
                    break;

                size_t const end = value.find_first_of('}', offset);
                if (end == value.npos)
                    break;

                cc::string const subs = value.substr(offset + 1, end - offset - 1);
                cc::string const& src = tmplMap[subs];
                value.replace(offset, (end - offset) + 1, src);

                modified = true;
            }

            if (modified)
                targetMap[pair.first] = pair.second;
        }
    }

    static cc::string getString(Json::Value const& in, char const* const name, cc::string const& defaultValue)
    {
        const Json::Value& v = in[name];
        if (!v.isString())
            return defaultValue;
        return v.asString();
    }

#if 0
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
#endif // 0

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

    static void generateRule(cc::string const& name, cc::string const& format, cc::script::rules& rules)
    {
        cc::script::rule r;
        r.name = name;

        // turn the string into a set of rules
        for (size_t offset = 0; offset < format.size(); offset++)
        {
            // escapement outside of a block; add a character block
            if (format[offset] == '\\')
            {
                if (++offset > format.size())
                    break;

                cc::script::rule::block b;
                b.set.push_back(format[offset]);
                b.rate = consumeRate(format, offset);
                r.blocks.emplace_back(b);
            }

            // add the set to a new block
            else if (format[offset] == '[')
            {
                r.blocks.emplace_back();
                cc::script::rule::block& b = r.blocks.back();

                for (size_t pos = offset + 1; pos < format.size(); pos++)
                {
                    if (format[pos] == '\\')
                    {
                        if (++pos > format.size())
                            break;

                        b.set.push_back(format[pos]);
                    }

                    else if (format[pos] == ']')
                    {
                        offset = pos;
                        b.rate = consumeRate(format, offset);
                        break;
                    }

                    else
                        b.set.push_back(format[pos]);
                }
            }

            // 'any' constraint
            else if (format[offset] == '.')
            {
                cc::script::rule::block b;
                b.rate = consumeRate(format, offset);
                r.blocks.emplace_back(b);
            }

            // non-special character; must be format spec
            else
            {
                cc::script::rule::block b;
                b.set.push_back(format[offset]);
                b.rate = consumeRate(format, offset);
                r.blocks.emplace_back(b);
            }
        }

        rules.emplace(name, r);
    }

    static size_t processRule(cc::script::rule const& r, char const* const start);

    // returns SIZE_MAX if the block cannot be satisfied, the number of consumed characters otherwise
    static size_t processBlock(cc::script::rule::block const& block, char const* const start)
    {
        switch (block.rate)
        {
            case cc::script::rule::rate_of::kOnce:
                if (block.set.find(*start) != block.set.npos)
                    return 1;
                return SIZE_MAX;

            case cc::script::rule::rate_of::kZeroOrOne:
                if (block.set.find(*start) != block.set.npos)
                    return 1;
                return 0;

            case cc::script::rule::rate_of::kZeroOrMore:
            {
                size_t offset = 0;
                while (start[offset] && block.set.find(start[offset]) != block.set.npos)
                    offset++;
                return offset;
            } break;

            case cc::script::rule::rate_of::kOneOrMore:
            {
                size_t offset = 0;
                while (start[offset] && block.set.find(start[offset]) != block.set.npos)
                    offset++;
                if (offset == 0)
                    return SIZE_MAX;
                return offset;
            } break;

            default:
                assert(false);
                return SIZE_MAX;
        }
    }

    // returns SIZE_MAX if the rule cannot be satisfied, the number of consumed characters otherwise
    static size_t processRule(cc::script::rule const& r, char const* const start)
    {
        size_t offset = 0;

        // a rule is made of up blocks, each of which is mandatory.
        // a block is satisfied by a set of characters and a repetition count.

        // 0?x[{base16}]+
        //
        // '0', 0 or 1
        // 'x', exactly 1
        // one or more from the '{base16}' template

        // process each block
        for (size_t i = 0; i < r.blocks.length(); i++)
        {
            cc::script::rule::block const& b = r.blocks[i];

            size_t rv;

            // not an empty set ('any') so process the block
            if (!b.set.empty())
                rv = processBlock(b, start + offset);

            // this is an 'any' set so find the first successful block after
            // this one and stop there.
            else
            {
                rv = SIZE_MAX;
                bool found = false;
                for (size_t n = i + 1; !found && n < r.blocks.length(); n++)
                {
                    for (size_t pos = 0; !found && start[offset + pos]; pos++)
                    {
                        size_t const count = processBlock(r.blocks[n], start + offset + pos);
                        if (SIZE_MAX != count)
                        {
                            found = true;
                            rv = pos + count;
                            i++; // skip the next block since we found it
                        }
                    }
                }
            }

            // failed to process the block, so abort the entire rule
            if (rv == SIZE_MAX)
                return SIZE_MAX;

            offset += rv;
        }

        return offset;
    }
} // namespace [anonymous]

bool cc::script::loadLanguage(cc::string const& json, cc::script::rules& rules)
{
    KVMap tmplDef;
    KVMap lexDef;

    try
    {
        Json::Reader reader;
        Json::Value root;

        if (!reader.parse(json, root))
            throw Json::Exception(reader.getFormattedErrorMessages());

        if (root.empty())
            return true;

        {
            // load templates
            Json::Value tmpl = root["templates"];
            Json::Value::Members tmplNames = tmpl.getMemberNames();
            for (Json::String name : tmplNames)
                tmplDef[name] = getString(tmpl, name.c_str(), "");

            injectTemplates(tmplDef, tmplDef);
        }

        // load lexemes
        {
            Json::Value lex = root["lexemes"];
            Json::Value::Members lexNames = lex.getMemberNames();

            for (Json::String name : lexNames)
                lexDef[name] = getString(lex, name.c_str(), "");

            injectTemplates(lexDef, tmplDef);

            for (KVPair pair : lexDef)
                generateRule(pair.first, pair.second, rules);
        }

        // load language
    }
    catch (Json::Exception& e)
    {
        printf("Exception: %s\n", e.what());
        return false;
    }


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

        for (cc::pair<cc::string, cc::script::rule> const& r : rules)
        {
            size_t const rv = processRule(r.second, cur);
            if (rv == SIZE_MAX)
                continue;

            if (rv > bestLength)
            {
                bestRule = r.first;
                bestLength = rv;
            }
            else if (rv == bestLength && bestRule.empty())
            {
                bestRule = r.first;
                bestLength = rv;
            }
        }

        assert(bestLength > 0); // couldn't find a rule for this token

        tokens.emplace_back(token{ bestRule, cur, cur + bestLength });

        cur += bestLength;
    }

    return false;
}
