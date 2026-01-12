#include <common/file.h>
#include <utility/console.h>
#include <utility/database.h>

#pragma warning(push,1)
#include <external/sqlite3/sqlite3.h>
#pragma warning(pop)

#pragma comment(lib, "sqlite3.lib")

namespace cc
{
    database::database(console& con)
        : m_console(con)
    {
    }

    database::database(console& con, char const* const filename)
        : m_console(con)
    {
        (void)open(filename);
    }

    database::~database()
    {
        close();
    }

    bool database::open(char const* const filename)
    {
        if (nullptr != m_db)
            close();

        if (!file::exists(filename))
            return false;

        return create(filename);
    }

    bool database::create(char const* const filename)
    {
        if (nullptr != m_db)
            close();

        int const dberr = sqlite3_open(filename, &m_db);
        if (SQLITE_OK != dberr)
        {
            m_console.logf(Source::kApp, Level::kError, "sqlite3_open failed with error %d; %s", dberr, sqlite3_errmsg(m_db));
            assert(SQLITE_OK == dberr);
        }
        return SQLITE_OK == dberr;
    }

    void database::close()
    {
        if (nullptr == m_db)
            return;

        for (script_pair pr : m_scripts)
        {
            assert(nullptr != pr.second);
            sqlite3_finalize(pr.second);
        }

        sqlite3_close(m_db);
        m_db = nullptr;
    }

    database::script database::add(char const* const sql, size_t length)
    {
        size_t const size = length == SIZE_MAX ? strlen(sql) : length;
        hash64 const hash = hash64_spookyv2(sql, size);

        script_map::iterator iter = m_scripts.find(hash);
        if (m_scripts.end() != iter)
            return script{ iter->first };

        sqlite3_stmt* stmt{};
        int const dberr = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
        if (SQLITE_OK != dberr)
        {
            m_console.logf(Source::kApp, Level::kError, "sqlite3_prepare_v2 failed with error %d; %s", dberr, sqlite3_errmsg(m_db));
            assert(SQLITE_OK == dberr);
            return script{};
        }

        m_scripts[hash] = stmt;
        return script{ hash };
    }

    void database::remove(script scr)
    {
        script_map::iterator iter = m_scripts.find(scr.hash);
        if (m_scripts.end() == iter)
            return;

        assert(nullptr != iter->second);
        sqlite3_finalize(iter->second);

        m_scripts.erase(iter);
    }
} // namespace cc
