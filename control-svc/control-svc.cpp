#include <common/platform/windows.h>
#include <common/stdlib.h>
#include <common/string.h>

#include <control-lib/control-lib.h>

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;

constexpr const char* kServiceName = "ChaosControl-Svc";

VOID ReportSvcStatus(DWORD dwCurrentState,
                      DWORD dwWin32ExitCode,
                      DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) ||
           (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else gSvcStatus.dwCheckPoint = dwCheckPoint++;

    // Report the status of the service to the SCM.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

VOID SvcReportEvent(char const* fn)
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    char buffer[80];

    hEventSource = RegisterEventSource(NULL, kServiceName);

    if (NULL != hEventSource)
    {
        sprintf_s(buffer, sizeof(buffer), "%s failed with %d", fn, GetLastError());

        lpszStrings[0] = kServiceName;
        lpszStrings[1] = buffer;

        ReportEvent(hEventSource,        // event log handle
                    EVENTLOG_ERROR_TYPE, // event type
                    0,                   // event category
                    0,           // event identifier
                    NULL,                // no security identifier
                    2,                   // size of lpszStrings array
                    0,                   // no binary data
                    lpszStrings,         // array of strings
                    NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}

VOID WINAPI SvcCtrlHandler(DWORD ctrlCode)
{
    switch (ctrlCode)
    {
        case SERVICE_CONTROL_STOP:
            ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
            SetEvent(ghSvcStopEvent);
            ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
            return;

        case SERVICE_CONTROL_INTERROGATE:
            break;

        default:
            break;
    }
}

VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    HMODULE module{ nullptr };
    control_api api{};
    control_lib* lib{ nullptr };

    do
    {
        ghSvcStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (ghSvcStopEvent == NULL)
        {
            SvcReportEvent("CreateEvent");
            break;
        }

        gSvcStatusHandle = RegisterServiceCtrlHandler(kServiceName, SvcCtrlHandler);
        if (!gSvcStatusHandle)
        {
            SvcReportEvent("RegisterServiceCtrlHandler");
            break;
        }

        gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        gSvcStatus.dwServiceSpecificExitCode = 0;

        // FIXME: come up with a good hint time for how long it could take
        // the service to start.
        DWORD kWaitHintInMs = 3000;
        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, kWaitHintInMs);

        module = LoadLibraryA("control-lib.dll");
        if (nullptr == module)
        {
            SvcReportEvent("LoadLibrary");
            break;
        }

        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, kWaitHintInMs);

        get_api_fn getApi;
        *(void**)&getApi = GetProcAddress(module, kGetAPIName);
        if (nullptr == getApi)
        {
            SvcReportEvent("GetProcAddress");
            break;
        }

        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, kWaitHintInMs);

        getApi(&api);

        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, kWaitHintInMs);

        lib = api.create(__argc, __argv);
        if (nullptr == lib)
        {
            SvcReportEvent("api.create");
            break;
        }

        ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, kWaitHintInMs);

        if (!api.start(lib))
        {
            SvcReportEvent("api.start");
            break;
        }

        ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

        while (1)
        {
            // wait for the stop flag to be set. note that the stop request
            // sets SERVICE_STOP_PENDING
            constexpr DWORD kLoopDelayMs = 1000;
            if (WAIT_OBJECT_0 == WaitForSingleObject(ghSvcStopEvent, kLoopDelayMs))
                break;

            if (!api.update(lib))
            {
                ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
                break;
            }

            ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
        }
    } while (false);

    if (nullptr != lib)
    {
        api.stop(lib);
        api.destroy(lib);
    }

    if (nullptr != module)
    {
        FreeLibrary(module);
    }

    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { (char*)kServiceName, (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcher(DispatchTable))
        SvcReportEvent("StartServiceCtrlDispatcher");

    return 0;
}
