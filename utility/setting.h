#pragma once

#include <common/initializer_list.h>
#include <common/string.h>
#include <common/type_traits.h>
#include <common/types.h>
#include <common/variant.h>
#include <containers/unordered_map.h>
#include <containers/vector.h>

namespace Json
{
    class Value;
} // namespace Json

namespace cc
{
    class setting_ctor_helper;

    class setting
    {
    public:
        using map_type = unordered_map<string, setting>;
        using var_type = variant<bool, int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, float, double, string, map_type>;

        class asa_c
        {
        public:
            asa_c(const setting& setting) : m_setting(setting) {}

            asa_c(asa_c&&) = default;

            template <typename T>
            operator const T&() const
            {
                if constexpr (cc::is_same_v<const T&, const setting&>)
                    return m_setting;
                else
                    return m_setting.value();
            }

        private:
            asa_c() = delete;
            asa_c(const asa_c&) = delete;
            asa_c operator=(const asa_c&) = delete;
            asa_c& operator=(asa_c&&) = delete;

            const setting& m_setting;
        };

        class asa_m
        {
        public:
            asa_m(setting& setting) : m_setting(setting) {}
            asa_m(asa_m&&) = default;

            template <typename T>
            operator T() const
            {
                if constexpr (cc::is_same_v<T, setting>)
                    return m_setting;
                else
                    return m_setting.value();
            }

            template <typename T>
            asa_m& operator=(const T& v)
            {
                if constexpr (cc::is_same_v<T&, setting&>)
                    m_setting.value() = v.value();
                else if constexpr (cc::is_same_v<const T&, const setting&>)
                    m_setting.value() = v.value();
                else
                    m_setting.value() = v;
                return *this;
            }

        private:
            asa_m() = delete;
            asa_m(const asa_m&) = delete;
            asa_m operator=(const asa_m&) = delete;
            asa_m& operator=(asa_m&&) = delete;

            setting& m_setting;
        };

        static setting read(const char* const filename);

        explicit setting(const char* name, const var_type& value);
        explicit setting(const char* name, const Json::Value& value);

        setting() = default;
        setting(setting&&) = default;;
        setting& operator=(setting&&) = default;
        setting(const setting&) = default;
        setting& operator=(const setting&) = default;
        ~setting() = default;

        const string& name() const { return m_name; }

        var_type& value() { return m_var; }
        const var_type& value() const { return m_var; }

        map_type::iterator begin() { return m_children.begin(); }
        map_type::iterator end() { return m_children.end(); }

        map_type::const_iterator begin() const { return m_children.begin(); }
        map_type::const_iterator end() const { return m_children.end(); }

        bool contains(const char* name) const;

        // get named child or path in the form:
        // single child:
        //   Name
        // path from current setting to descendant
        //   Child/Grandchild/Name
        // path from root to descendant:
        //   /Parent/Child/Grandchild/Name
        asa_m operator[](const char* path);
        const asa_c operator[](const char* path) const;

        bool write(const char* filename, const setting&) const;

        const setting* find(const char*) const;
        setting* find(const char*);
        setting& find_or_add(const char*);

    private:

        string m_name{};
        var_type m_var{};
        setting* m_parent{};
        map_type m_children;
    };

} // namespace cc

#include "setting.inl"
