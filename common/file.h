#pragma once

#include <common/compiler.h>
#include <common/memory.h>
#include <common/string.h>
#include <common/types.h>

namespace cc
{
    enum class file_mode
    {
        kInvalid,
        kRead,
        kWrite,
        kAppend,
    };

    enum class file_type
    {
        kInvalid,
        kText,
        kBinary,
    };

    enum class file_pos
    {
        kInvalid,
        kStart,
        kCurr,
        kEnd,
    };

    class file
    {
    public:
        static unique_ptr<char[]> load(char const* path, file_type);
        static bool exists(char const* path);

        file() = default;
        file(char const* path, file_mode, file_type);
        file(file const&);
        file(file&&) noexcept;
        ~file();
        file& operator=(file const&);
        file& operator=(file&&) noexcept;
        operator bool() const { return m_platform != nullptr; }
        bool operator !() const { return m_platform == nullptr; }

        bool open(char const* const path, file_mode const, file_type const);
        void close();

        file_mode mode() const { return m_mode; }
        file_type type() const { return m_type; }
        char const* path() const { return m_path; }
        size_t size() const;

        bool seek(size_t const offset, file_pos const);
        bool seek(ssize_t const offset, file_pos const);
        size_t tell() const;

        void flush();

        size_t read(void* const buffer, size_t const buffer_size);
        size_t write(void const* const buffer, size_t const buffer_size);

        template<typename Type, size_t Count>
        size_t read(Type(&buffer)[Count])
        {
            return read(buffer, sizeof(Type) * Count);
        }

        size_t write(cc::string const& str)
        {
            return write(str.c_str(), str.length());
        }

        size_t write(char const* const str)
        {
            return write(str, strlen(str));
        }

        template<typename Type, size_t Count>
        size_t write(Type const (&buffer)[Count])
        {
            return write(buffer, sizeof(Type) * Count);
        }

    private:
        void* m_platform = nullptr;
        char* m_path = nullptr;
        file_mode m_mode = file_mode::kInvalid;
        file_type m_type = file_type::kInvalid;
    };

    bool read_file(char const* const path, string& buffer);

} // namespace cc
