#pragma once

#include <common/types.h>
#include <containers/string.h>
#include <containers/unordered_map.h>
#include <containers/vector.h>

namespace cc::script
{
    struct rule;

    struct token
    {
        string rule;
        char const* begin;
        char const* end;
    };

    using tokens = cc::vector<token>;

    enum class constraint_type : uint64_t
    {
        kAsChar,
        kAsString,
        kAsConstraint,
    };

    class constraint
    {
    public:
        constraint() = default;
        ~constraint();
        constraint(constraint const&);
        constraint(constraint&&) noexcept;

        constraint& operator=(constraint const&);
        constraint& operator=(constraint&&) noexcept;

        explicit constraint(char const);
        explicit constraint(char const* const, constraint_type const = constraint_type::kAsString);
        explicit constraint(string const&, size_t const offset, size_t const length, constraint_type const = constraint_type::kAsString);

        constraint_type type() const { return m_type; }

        char asChar() const;
        char const* asString() const;

    private:
        union value
        {
            char asChar;
            char* asString;

            value() = default;
            ~value() = default;

            explicit value(char const);
            explicit value(char const* const);
        };
        
        constraint_type m_type{};
        value m_value{};
    };

    struct rule
    {
        enum class rate_of : size_t
        {
            kOnce,
            kZeroOrOne,
            kZeroOrMore,
            kOneOrMore,
        };

        struct block
        {
            rate_of rate = rate_of::kOnce;
            vector<constraint> set;
        };

        string name;
        string description;
        vector<block> blocks;
        int32_t priority;
        bool tokenize;
        uint8_t pad[3]{};
    };

    using rules = unordered_map<string, rule>;

    // verify that the rule set is complete:
    //   - each rule has at least one required character
    //   - all constraints referenced are found
    bool validate(rules const&);

} // namespace cc::script
