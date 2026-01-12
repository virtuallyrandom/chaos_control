#include <utility/crash_handler.h>

#include <common/assert.h>
#include <common/string.h>
#include <common/platform/windows.h>

#pragma warning(push, 1)

#pragma warning(disable:4514) // unreferenced inline function has been removed
#pragma warning(disable:4820) // 'n' bytes padding added after data member

#include <DbgHelp.h>

#pragma warning(pop)

namespace
{
    static LONG WINAPI exceptionHandler(EXCEPTION_POINTERS* ExceptionInfo) throw()
    {
        cc::utility::crash_handler* const crashHandler = cc::utility::crash_handler::getTop();

        HMODULE const lib = LoadLibraryA("DbgHelp.dll");
        if (lib != nullptr)
        {
            void* const procaddr = GetProcAddress(lib, "MiniDumpWriteDump");
            if (procaddr != nullptr)
            {
                BOOL(WINAPI* myMiniDumpWriteDump)(HANDLE,
                                                  DWORD,
                                                  HANDLE,
                                                  MINIDUMP_TYPE,
                                                  PMINIDUMP_EXCEPTION_INFORMATION,
                                                  PMINIDUMP_USER_STREAM_INFORMATION,
                                                  PMINIDUMP_CALLBACK_INFORMATION);

                memcpy(&myMiniDumpWriteDump, &procaddr, sizeof(myMiniDumpWriteDump));

                HANDLE const file = CreateFileA(crashHandler->getFilename(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
                if (file != INVALID_HANDLE_VALUE)
                {
                    MINIDUMP_EXCEPTION_INFORMATION excInfo{};
                    MINIDUMP_USER_STREAM_INFORMATION userInfo{};
                    MINIDUMP_CALLBACK_INFORMATION callbackInfo{};

                    excInfo.ThreadId = GetCurrentThreadId();
                    excInfo.ExceptionPointers = ExceptionInfo;
                    excInfo.ClientPointers = FALSE;

                    myMiniDumpWriteDump(GetCurrentProcess(),
                                        GetCurrentProcessId(),
                                        file,
                                        MiniDumpNormal,
                                        &excInfo,
                                        &userInfo,
                                        &callbackInfo);

                    CloseHandle(file);
                }
            }
            FreeLibrary(lib);
        }

        cc::utility::crash_handler_cb const cb = crashHandler->getCallback();
        void* const cbParam = crashHandler->getCallbackParam();

        if (cb != nullptr)
            cb(cbParam);

        return 0;
    }
} // namespace [anonymous]

namespace cc::utility
{
    crash_handler::crash_handler(char const* const dumpFile, crash_handler_cb cb, void* const param)
        : m_cb(cb)
        , m_cbParam(param)
    {
        assert(dumpFile && dumpFile[0]);

        snprintf(m_filename, sizeof(m_filename), "%s", dumpFile);
        m_filename[sizeof(m_filename) - 1] = 0;

        m_next = s_top;
        s_top = this;

        SetUnhandledExceptionFilter(exceptionHandler);
    }

    crash_handler::~crash_handler()
    {
        s_top = s_top->m_next;
        SetUnhandledExceptionFilter(s_top ? exceptionHandler : nullptr);

    }
} // namespace cc::utility
