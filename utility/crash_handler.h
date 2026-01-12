#pragma once

#include <common/compiler.h>
#include <common/types.h>

namespace cc::utility
{
    using crash_handler_cb = void(*)(void* const);

    class crash_handler
    {
    public:
        crash_handler(char const* const dumpFile, crash_handler_cb = nullptr, void* const crash_handler_param = nullptr);
        ~crash_handler();

        const char* getFilename() const { return m_filename; }
        crash_handler_cb getCallback() const { return m_cb; }
        void* getCallbackParam() const { return m_cbParam; }

        static crash_handler* getTop() { return s_top; }

    private:
        compiler_disable_copymove(crash_handler);

        crash_handler_cb m_cb = nullptr;
        void* m_cbParam = nullptr;
        crash_handler* m_next = nullptr;
        char m_filename[264]{};

        static inline crash_handler* s_top = nullptr;
    };
} // namespace cc::utility
