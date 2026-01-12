#pragma once

#include <common/assert.h>
#include <containers/vector.h>
#include <utility/console.h>
#include <utility/database.h>

#pragma warning(push,1)
#include <external/sqlite3/sqlite3.h>
#pragma warning(pop)

namespace cc
{
    template <class... Args>
    bool database::exec(script scr, Args&&... args)
    {
        sqlite3_stmt* const stmt = m_scripts[scr.hash];
        assert(nullptr != stmt);

        return exec(stmt, cc::forward<Args>(args)...);
    }

    template <class... Args>
    bool database::exec(char const* const sql, Args&&... args)
    {
        int dberr;

        sqlite3_stmt* stmt{};
        dberr = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
        if (SQLITE_OK != dberr)
        {
            m_console.logf(Source::kApp, Level::kError, "sqlite3_prepare_v2 failed with error %d; %s", dberr, sqlite3_errmsg(m_db));
            return false;
        }

        bool const success = exec(stmt, cc::forward<Args>(args)...);

        dberr = sqlite3_finalize(stmt);
        if (SQLITE_OK != dberr)
        {
            m_console.logf(Source::kApp, Level::kError, "sqlite3_finalize failed with error %d; %s", dberr, sqlite3_errmsg(m_db));
            return false;
        }

        return success;
    }

    inline uint64_t database::last_insert_rowid() const
    {
        assert(nullptr != m_db);
        int64_t const value = sqlite3_last_insert_rowid(m_db);
        return truncate_cast<uint64_t>(value);
    }

    template <class... Args>
    bool database::exec(sqlite3_stmt* const stmt, Args&&... args)
    {
        int dberr;

        callback cb{};
        callback_with_param<void*> cb_with_param{};
        void* cb_param{};

        dberr = db_bind_generic(stmt, 1, &cb, &cb_with_param, &cb_param, cc::forward<Args>(args)...);
        if (SQLITE_OK != dberr)
        {
            m_console.logf(Source::kDatabase, Level::kError, "Unable to bind arguments");
            return false;
        }

        dberr = sqlite3_step(stmt);
        if (SQLITE_ROW == dberr)
        {
            // use the first row to map the column names and initial values
            // to vectors. every subsequent call maps data into the same
            // indices.
            int const numCols = sqlite3_column_count(stmt);
            if (numCols < 0)
                return false;

            cc::vector<char const*> names;
            cc::vector<variant_type> values;
            names.resize(numCols);
            values.resize(numCols);

            for (int i = 0; i < numCols; i++)
                names[i] = sqlite3_column_name(stmt, i);

            do
            {
                for (int i = 0; i < numCols; i++)
                {
                    switch (sqlite3_column_type(stmt, i))
                    {
                        case SQLITE_INTEGER:
                            values[i] = sqlite3_column_int64(stmt, i);
                            break;

                        case SQLITE_FLOAT:
                            values[i] = sqlite3_column_double(stmt, i);
                            break;

                        case SQLITE_BLOB:
                        {
                            blob b;
                            b.data = sqlite3_column_blob(stmt, i);
                            b.size = sqlite3_column_bytes(stmt, i);
                            values[i] = b;
                        } break;

                        case SQLITE_TEXT:
                        {
                            uint8_t const* const value = sqlite3_column_text(stmt, i);
                            values[i] = reinterpret_cast<char const*>(value);
                        } break;

                        case SQLITE_NULL:
                            values[i] = nullptr;
                            break;

                        default:
                            break;
                    }
                }

                if (cb && !cb(truncate_cast<size_t>(numCols), &names[0], &values[0]))
                    break;

                else if (cb_with_param && !cb_with_param(cb_param, truncate_cast<size_t>(numCols), &names[0], &values[0]))
                    break;

                dberr = sqlite3_step(stmt);
            } while (SQLITE_ROW == dberr);
        }

        if (SQLITE_ROW != dberr && SQLITE_DONE != dberr)
            m_console.logf(Source::kDatabase, Level::kError, "Failed to execute script: %s", sqlite3_errmsg(m_db));

        (void)sqlite3_reset(stmt);

        return SQLITE_ROW == dberr || SQLITE_DONE == dberr;
    }

    // disable unreachable code warning
#pragma warning(push)
#pragma warning(disable:4702)

    inline int database::db_bind_generic(sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**)
    {
        return SQLITE_OK;
    }

    template <class Arg1, class... Args>
    inline int database::db_bind_generic(sqlite3_stmt* const stmt, int index, cc::database::callback* cb, cc::database::callback_with_param<void*>* cb_with_param, void** cb_param, Arg1 first)
    {
        return db_bind_generic(stmt, index, cb, cb_with_param, cb_param, first, nullptr);
    }

    template <class Arg1, class Arg2, class... Args>
    inline int database::db_bind_generic(sqlite3_stmt* const stmt, int index, cc::database::callback* cb, cc::database::callback_with_param<void*>* cb_with_param, void** cb_param, Arg1 first, Arg2 second, Args&&... args)
    {
        int dberr{ SQLITE_OK };
        bool const b = db_bind(dberr, stmt, index, cb, cb_with_param, cb_param, first, second);

        if (SQLITE_OK != dberr)
            return dberr;

        if (b)
            return db_bind_generic(stmt, index + 1, cb, cb_with_param, cb_param, cc::forward<Args>(args)...);
        else
            return db_bind_generic(stmt, index + 1, cb, cb_with_param, cb_param, second, cc::forward<Args>(args)...);
    }

    template <typename As, class Arg>
    inline bool database::db_read(As* const to, Arg first)
    {
        if constexpr (cc::is_same_v<Arg, As>)
        {
            *to = first;
            return true;
        }

        else if constexpr (cc::is_same_v<Arg, nullptr_t>)
            return false;

        else
        {
            typedef typename Arg::UnhandledType Error;
            Error T;
            (void)T;
            static_assert(false, "Unexpected type");
        }

        return false;
    }

    inline bool database::db_bind(int& dberr, sqlite3_stmt*, int, cc::database::callback*, cc::database::callback_with_param<void*>*, void**)
    {
        dberr = SQLITE_OK;
        return false;
    }

    template <class Arg1>
    inline bool database::db_bind(int& dberr, sqlite3_stmt* const stmt, int index, cc::database::callback* cb, cc::database::callback_with_param<void*>* cb_with_param, void** cb_param, Arg1 first)
    {
        return db_bind(dberr, stmt, index, cb, cb_with_param, cb_param, first, nullptr);
    }

    template <class Arg1, class Arg2>
    inline bool database::db_bind(int& dberr, sqlite3_stmt* const stmt, int index, cc::database::callback* cb, cc::database::callback_with_param<void*>* cb_with_param, void** cb_param, Arg1 first, Arg2 second)
    {
        if constexpr (cc::is_same_v<cc::remove_const_t<Arg1>, char*>)
            dberr = sqlite3_bind_text(stmt, index, first, -1, nullptr);

        else if constexpr (cc::is_same_v<cc::remove_const_t<Arg1>, string&>)
            dberr = sqlite3_bind_text(stmt, index, first.c_str(), first.size(), nullptr);

        else if constexpr (cc::is_integral_v<Arg1>)
        {
            if constexpr (sizeof(first) <= 4)
            {
                int32_t const value = truncate_cast<int32_t>(first);
                dberr = sqlite3_bind_int(stmt, index, value);
            }
            else if constexpr (sizeof(first) <= 8)
            {
                int64_t const value = truncate_cast<int64_t>(first);
                dberr = sqlite3_bind_int64(stmt, index, value);
            }
            else
            {
                static_assert(sizeof(first) <= 8, "Unhandled size");
            }
        }

        else if constexpr (cc::is_floating_point_v<Arg1>)
        {
            double const value = truncate_cast<double>(first);
            dberr = sqlite3_bind_double(stmt, index, value);
        }

        else if constexpr (cc::is_same_v<Arg1, cc::database::callback>)
        {
            // callback can only be specified once
            assert(*cb == nullptr && *cb_with_param == nullptr);
            *cb = first;
            dberr = SQLITE_OK;
        }

        else if constexpr (cc::is_same_v<Arg1, cc::remove_const_t<blob&>>)
            dberr = sqlite3_bind_blob(stmt, index, second.data, truncate_cast<int>(second.size), SQLITE_STATIC);

        else if constexpr (cc::is_same_v<Arg1, cc::remove_const_t<blob*>>)
            dberr = sqlite3_bind_blob(stmt, index, second->data, truncate_cast<int>(second->size), SQLITE_STATIC);

        else if constexpr (cc::is_same_v<Arg1, cc::database::callback_with_param<Arg2>>)
        {
            // callback can only be specified once
            assert(*cb == nullptr && *cb_with_param == nullptr);
            memcpy(cb_with_param, &first, sizeof(first));
            Arg2 param{};
            bool const found_param = db_read<Arg2>(&param, second);
            if (found_param)
                memcpy(cb_param, &param, sizeof(param));
            assert(found_param); // cb_with_param read without a param
            return found_param;
        }

        // blob and size
        else if constexpr (cc::is_pointer_v<Arg1> && cc::is_integral_v<Arg2>)
        {
            size_t size{};
            if constexpr (cc::is_unsigned_v<Arg2>)
                db_read<Arg2>(&size, second);
            else
            {
                ssize_t ssize{};
                db_read<Arg2>(&ssize, second);
                size = truncate_cast<size_t>(ssize);
            }
            dberr = sqlite3_bind_blob(stmt, index, first, truncate_cast<int>(size), SQLITE_STATIC);
            return true;
        }

        else if constexpr (cc::is_same_v<Arg1, nullptr_t>)
            return false;

        else
        {
#if 1
            m_console.logf(Source::kDatabase, Level::kError, "Unexpected argument pair:\n    Arg1: %s\n    Arg2: %s\n", typeid(Arg1).name(), typeid(Arg2).name());
            assert(false);
#else
            const char* astr = typeid(Arg1).name();
            const char* bstr = typeid(cc::database::callback_with_param<Arg2>).name();
            const char* bstr2 = typeid(Arg2).name();
            const char* bstr3 = typeid(second).name();
            const bool same = cc::is_same_v<Arg1, cc::database::callback_with_param<Arg2>>;

            const int diff = strcmp(astr, bstr);
            (void)diff;
            (void)astr;
            (void)bstr;
            (void)bstr2;
            (void)bstr3;
            (void)same;

#endif
        }

        return false;
    }

#pragma warning(pop)
} // namespace cc