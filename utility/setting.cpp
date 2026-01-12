#include "setting.h"

#include <common/assert.h>
#include <common/file.h>
#include <common/memory.h>

#pragma warning(push,1)
#include <external/json/json.h>
#pragma warning(pop)

namespace cc
{
    setting::setting(const char* const name, const var_type& value)
        : m_name(name ? name : "")
        , m_var(value)
    {
    }

    setting::setting(const char* const name, const Json::Value& value)
        : m_name(name ? name : "")
    {
        switch (value.type())
        {
            case Json::nullValue:
                break;

            case Json::intValue:
                m_var = value.asInt64();
                break;

            case Json::uintValue:
                m_var = value.asUInt64();
                break;

            case Json::realValue:
                m_var = value.asDouble();
                break;

            case Json::stringValue:
                m_var = value.asCString();
                break;

            case Json::booleanValue:
                m_var = value.asBool();
                break;

            case Json::arrayValue:
            {
                for (Json::ArrayIndex i = 0; i < value.size(); i++)
                {
                    char tmp[32];
                    sprintf_s(tmp, "%zu", m_children.length());
                    std::pair<map_type::iterator, bool> pr = m_children.emplace(tmp, setting{ tmp, value[i] });
                    pr.first->second.m_parent = this;
                }
            } break;

            case Json::objectValue:
            {
                Json::Value::Members members = value.getMemberNames();
                for (string key : members)
                {
                    std::pair<map_type::iterator, bool> pr = m_children.emplace(key, setting{ key.c_str(), value[key] });
                    pr.first->second.m_parent = this;
                }
            } break;
        }
    }

    bool setting::contains(const char* const path) const
    {
        return find(path) != nullptr;
    }

    setting::asa_m setting::operator[](const char* const path)
    {
        return asa_m(find_or_add(path));
    }

    const setting::asa_c setting::operator[](const char* const path) const
    {
        const setting* const setting = find(path);
        assert(setting != nullptr);
        return asa_c(*setting);
    }

    setting& setting::find_or_add(const char* const path)
    {
        assert(path != nullptr && path[0] != 0);

        if (path[0] == '/' && m_parent != nullptr)
            return m_parent->find_or_add(path);

        string rest;

        // not a root request; use the whole path
        if (path[0] != '/')
            rest = path;

        // root request, skip the first character as we've already rewound to
        // the root based on the above parent find_or_add.
        else
        {
            assert(m_name[0] == 0);
            rest = path + 1;
        }

        setting* pos = this;
        while (!rest.empty())
        {
            size_t index = rest.find('/');
            if (rest.npos == index)
                index = rest.length();

            setting& next = pos->m_children[rest.substr(0, index)];
            next.m_parent = pos;
            pos = &next;

            if (index == rest.length())
                break;

            rest = rest.substr(index + 1, rest.length());
        }

        return *pos;
    }

    const setting* setting::find(const char* const path) const
    {
        assert(path != nullptr && path[0] != 0);

        if (path[0] == '/' && m_parent != nullptr)
            return m_parent->find(path);

        string rest;

        // not a root request; use the whole path
        if (path[0] != '/')
            rest = path;

        // root request, skip the first character as we've already rewound to
        // the root based on the above parent find_or_add.
        else
        {
            assert(m_name[0] == 0);
            rest = path + 1;
        }

        const setting* pos = this;
        while (!rest.empty())
        {
            size_t index = rest.find('/');
            if (rest.npos == index)
                index = rest.length();

            if (!pos->m_children.contains(rest.substr(0, index)))
                return nullptr;

            pos = &pos->m_children.at(rest.substr(0, index));

            if (index == rest.length())
                break;

            rest = rest.substr(index + 1, rest.length());
        }

        return pos;
    }

    setting* setting::find(const char* const path)
    {
        assert(path != nullptr && path[0] != 0);

        if (path[0] == '/' && m_parent != nullptr)
            return m_parent->find(path);

        string rest;

        // not a root request; use the whole path
        if (path[0] != '/')
            rest = path;

        // root request, skip the first character as we've already rewound to
        // the root based on the above parent find_or_add.
        else
        {
            assert(m_name[0] == 0);
            rest = path + 1;
        }

        setting* pos = this;
        while (!rest.empty())
        {
            size_t index = rest.find('/');
            if (rest.npos == index)
                index = rest.length();

            if (!pos->m_children.contains(rest.substr(0, index)))
                return nullptr;

            pos = &pos->m_children.at(rest.substr(0, index));

            if (index == rest.length())
                break;

            rest = rest.substr(index + 1, rest.length());
        }

        return pos;
    }

#if 0
    void serialize(const setting& stg, Json::Value& value)
    {
        switch (type)
        {
            case kNone:
                break;

            case kBool:
                value[m_name] = vBool;
                break;

            case kUint64:
                value[m_name] = vUint64;
                break;

            case kArray:
                break;

            case kString:
                value[m_name] = vString;
                break;
        }

        if (m_child != nullptr)
            m_child->serialize(value[m_name]);

        if (m_next != nullptr)
            m_next->serialize(value);
    }

    setting deserialize(const Json::Value& value)
    {
        setting stg;

        switch (value.type())
        {
            case Json::nullValue:
                break;

            case Json::intValue:
                stg.set(value.asInt64());
                break;

            case Json::uintValue:
                stg.set(value.asUInt64());
                break;

            case Json::realValue:
                stg.set(value.asDouble());
                break;

            case Json::stringValue:
                stg.set(value.asCString());
                break;

            case Json::booleanValue:
                stg.set(value.asBool());
                break;

            case Json::arrayValue:
            {
                for (Json::ArrayIndex i = 0; i < value.size(); i++)
                    stg.emplace_back(value[i].asCString(), deserialize(value[i]));
            } break;

            case Json::objectValue:
            {
                Json::Value::Members members = value.getMemberNames();
                for (string name : members)
                    stg.emplace_back(name.c_str(), value[name]);
            } break;
        }

        return stg;
    }

    const setting* setting::get(const char* path) const
    {
    }

    const setting* setting::get_child(const char* name) const
    {
        const setting* child = m_child;
        while (child != nullptr)
        {
            if (child->m_name == name)
                return child;
            child = child->m_next;
        }
        return nullptr;
    }

    const setting* setting::get_sibling(const char* name) const
    {
        const setting* sib = m_next;
        while (sib != nullptr)
        {
            if (sib->m_name == name)
                return sib;
            sib = sib->m_next;
        }
        return nullptr;
    }
#endif // 0

    setting setting::read(const char* const filename)
    {
        setting stg;

        cc::file settingsFile(filename, cc::file_mode::kRead, cc::file_type::kText);
        if (!settingsFile)
        {
            verify(true, settingsFile);
            return stg;
        }

        size_t bufferSize = settingsFile.size();
        cc::unique_ptr<char[]> buffer{ new char[bufferSize] };

        bufferSize = settingsFile.read(buffer.get(), bufferSize);

        Json::CharReaderBuilder builder;
        builder["collectComments"] = true;
        builder["allowComments"] = true;
        builder["allowTrailingCommas"] = true;
        builder["allowSingleQuotes"] = true;
        builder["rejectDupKeys"] = false;

        cc::unique_ptr<Json::CharReader> reader{ builder.newCharReader() };
        Json::Value root;
        Json::String err;
        if (!reader->parse(buffer.get(), buffer.get() + bufferSize, &root, &err))
            return stg;

        return setting{ "", root };
    }

    bool setting::write(const char* const filename, const cc::setting& settings) const
    {
        cc::file settingsFile(filename, cc::file_mode::kWrite, cc::file_type::kText);
        if (!settingsFile)
            return false;

        (void)settings;
        Json::Value root;// = serialize(settings, root);

        Json::StreamWriterBuilder builder;
        settingsFile.write(Json::writeString(builder, root));

        return true;
    }
} // namespace cc
