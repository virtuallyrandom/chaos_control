#include <common/file.h>

#include <common/assert.h>
#include <common/platform/platform_file.h>

namespace cc
{
    unique_ptr<char[]> file::load(char const* path, file_type t)
    {
        char* mem{};

        file f(path, file_mode::kRead, t);
        if (f)
        {
            size_t size = f.size();
            mem = new char[size + 1];
            size = f.read(mem, size);
            mem[size] = 0;
        }

        return unique_ptr<char[]>(mem);
    }

    bool file::exists(char const* path)
    {
        return 0 == stat(path, nullptr);
    }

    file::file(char const* const path, file_mode const m, file_type const t)
    {
        (void)open(path, m, t);
    }

    file::file(file const& f)
    {
        if (open(f.m_path, f.m_mode, f.m_type))
            (void)seek(f.tell(), file_pos::kStart);
    }

    file::file(file&& f) noexcept
    {
        m_platform = f.m_platform;
        f.m_platform = nullptr;

        m_path = f.m_path;
        f.m_path = nullptr;

        m_mode = f.m_mode;
        f.m_mode = file_mode::kInvalid;

        m_type = f.m_type;
        f.m_type = file_type::kInvalid;
    }

    file::~file()
    {
        close();
    }

    file& file::operator=(file const& f)
    {
        if (this == &f)
            return *this;

        close();

        if (f.m_platform == nullptr)
            return *this;

        if (open(f.m_path, f.m_mode, f.m_type))
            (void)seek(f.tell(), file_pos::kStart);

        return *this;
    }

    file& file::operator=(file&& f) noexcept
    {
        if (this == &f)
            return *this;

        m_platform = f.m_platform;
        f.m_platform = nullptr;

        m_path = f.m_path;
        f.m_path = nullptr;

        m_mode = f.m_mode;
        f.m_mode = file_mode::kInvalid;

        m_type = f.m_type;
        f.m_type = file_type::kInvalid;

        return *this;
    }

    bool file::open(char const* const path, file_mode const mode, file_type const type)
    {
        close();

        char const* modeValue[] =
        {
            nullptr, // kInvalid,
            "r",     // kRead,
            "w",     // kWrite,
            "w+",    // kAppend,
        };

        char const* typeValue[] =
        {
            nullptr, // kInvalid,
            "t", // kText,
            "b", // kBinary,
        };

        size_t const modeIndex = static_cast<size_t>(mode);
        assert(modeIndex < countof(modeValue) && modeValue[modeIndex] != nullptr);

        size_t const typeIndex = static_cast<size_t>(type);
        assert(typeIndex < countof(typeValue) && typeValue[typeIndex] != nullptr);

        char modeStr[4]{};

        strcat_s(modeStr, modeValue[modeIndex]);
        strcat_s(modeStr, typeValue[typeIndex]);
        modeStr[sizeof(modeStr) - 1] = 0;

        FILE* fp = nullptr;
        errno_t const err = fopen_s(&fp, path, modeStr);
        if (err != 0 || fp == nullptr)
            return false;

        m_platform = fp;

        size_t const pathSize = strlen(path) + 1;
        m_path = new char[pathSize];
        strcpy_s(m_path, pathSize, path);

        m_mode = mode;
        m_type = type;

        return true;
    }

    void file::close()
    {
        m_type = file_type::kInvalid;
        m_mode = file_mode::kInvalid;

        delete[] m_path;
        m_path = nullptr;

        if (m_platform != nullptr)
        {
            FILE* const fp = reinterpret_cast<FILE*>(m_platform);
            fflush(fp);
            fclose(fp);
            m_platform = nullptr;
        }
    }

    size_t file::size() const
    {
        if (m_platform == nullptr)
            return 0;

        FILE* const fp = reinterpret_cast<FILE*>(m_platform);
        ssize_t const originalPos = _ftelli64(fp);
        _fseeki64(fp, 0, SEEK_END);
        ssize_t const length = _ftelli64(fp);
        _fseeki64(fp, originalPos, SEEK_SET);
        return truncate_cast<size_t>(length);
    }

    bool file::seek(size_t const offset, file_pos const pos)
    {
        ssize_t const soffset = truncate_cast<ssize_t>(offset);
        return seek(soffset, pos);
    }

    bool file::seek(ssize_t const offset, file_pos const pos)
    {
        if (m_platform == nullptr)
            return false;

        FILE* const fp = reinterpret_cast<FILE*>(m_platform);

        int origins[] =
        {
            -1, // kInvalid,
            SEEK_SET, // kStart,
            SEEK_CUR, // kCurr,
            SEEK_END, // kEnd,
        };

        size_t const posIndex = static_cast<size_t>(pos);
        assert(posIndex < countof(origins) && pos != file_pos::kInvalid);

        int const rv = _fseeki64(fp, offset, origins[posIndex]);
        if (rv != 0)
        {
            assert(rv == 0);
            return false;
        }

        return true;
    }

    size_t file::tell() const
    {
        if (m_platform == nullptr)
            return SIZE_MAX;

        FILE* const fp = reinterpret_cast<FILE*>(m_platform);
        ssize_t const pos = _ftelli64(fp);
        return truncate_cast<size_t>(pos);
    }

    void file::flush()
    {
        if (m_platform != nullptr)
            fflush(reinterpret_cast<FILE*>(m_platform));
    }

    size_t file::read(void* const buffer, size_t const bufferSize)
    {
        if (m_platform == nullptr)
            return 0;

        FILE* const fp = reinterpret_cast<FILE*>(m_platform);
        size_t const len = fread(buffer, 1, bufferSize, fp);
        if (m_type == file_type::kText && len < bufferSize)
            reinterpret_cast<byte*>(buffer)[len] = byte(0);
        return len;
    }

    size_t file::write(void const* const buffer, size_t const bufferSize)
    {
        if (m_platform == nullptr)
            return 0;

        FILE* const fp = reinterpret_cast<FILE*>(m_platform);
        return fwrite(buffer, 1, bufferSize, fp);
    }

    bool read_file(char const* const path, string& buffer)
    {
        file f(path, file_mode::kRead, file_type::kText);

        if (!f)
            return false;

        size_t const fileSize = f.size();

        buffer.resize(fileSize + 1);
        size_t const actualLength = f.read(buffer.data(), fileSize + 1);
        buffer.resize(actualLength);

        f.close();

        return true;
    }

} // namespace cc
