#pragma once

#include <common/compiler.h>
#include <common/hash.h>
#include <common/utility.h>
#include <common/variant.h>
#include <containers/map.h>

struct sqlite3;
struct sqlite3_stmt;

namespace cc
{
    class console;

    class database
    {
    public:
        struct script { hash64 hash; };

        struct blob
        {
            const void* data{};
            size_t size{};
        };

        using variant_type = variant<nullptr_t, int64_t, double, blob, char const*>;

        using callback = bool (*)(size_t num, char const* const* names, variant_type const* values);

        template<typename Type>
        using callback_with_param = bool (*)(Type, size_t num, char const* const* names, variant_type const* values);

        database(console&);
        database(console&, char const* open_filename);
        ~database();

        bool open(char const* filename);
        bool create(char const* filename);
        void close();

        script add(char const* sql, size_t length = SIZE_MAX);
        void remove(script);

        // arguments can be one of these, which must match the passed
        // script, otherwise the execution will fail with an error:
        //   bool
        //   [u]int8_t
        //   [u]int16_t
        //   [u]int32_t
        //   [u]int64_t
        //   float
        //   double
        //   [const] char*
        //   [const] cc::string
        //   [const] blob
        //   any other [const] pointer followed by size_t
        // additionally, if a callback is passed, it will be called
        // for each item returned in the call (if any). the form of
        // the function must be one of:
        //   bool (*callback)(Param, char const** names, cc::variant* values) followed by a Param pointer
        //   bool (*callback)(char const** names, database::variant_type* values)
        // if a callback function are invoked, the first invocation
        // can be used to map names to indices for subsequent calls.
        template <class... Args>
        bool exec(script, Args&&... args);

        template <class... Args>
        bool exec(char const* sql, Args&&... args);

        uint64_t last_insert_rowid() const;

    private:
        using script_map = cc::map<hash64, sqlite3_stmt*>;
        using script_pair = cc::pair<hash64, sqlite3_stmt*>;

        template <class... Args>
        bool exec(sqlite3_stmt*, Args&&... args);

        inline int db_bind_generic(sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**);

        template <class Arg1, class... Args>
        inline int db_bind_generic(sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**, Arg1);

        template <class Arg1, class Arg2, class... Args>
        inline int db_bind_generic(sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**, Arg1, Arg2, Args&&...);

        template <typename As, class Arg>
        inline bool db_read(As*, Arg);

        inline bool db_bind(int&, sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**);

        template <class Arg1>
        inline bool db_bind(int&, sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**, Arg1);

        template <class Arg1, class Arg2>
        inline bool db_bind(int&, sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**, Arg1, Arg2);

        compiler_disable_copymove(database);

        console& m_console;
        sqlite3* m_db{};
        script_map m_scripts;
    };
} // namespace cc

#include <utility/database.inl>
