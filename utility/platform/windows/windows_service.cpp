#include <utility/service.h>

#include <common/assert.h>
#include <common/math.h>
#include <common/platform/windows.h>
#include <utility/console.h>

namespace
{
    bool stop_dependent_services(SC_HANDLE mgr, SC_HANDLE svc)
    {
        DWORD size;
        DWORD count;

        DWORD start = GetTickCount();
        DWORD timeout = 30000; // 30-second time-out

        // if successful, there are no dependencies so just bail
        if (EnumDependentServices(svc, SERVICE_ACTIVE, nullptr, 0, &size, &count))
            return true;

        assert(GetLastError() == ERROR_MORE_DATA);

        ENUM_SERVICE_STATUS* const depStatus = new ENUM_SERVICE_STATUS[count];
        SC_HANDLE* const depService = new SC_HANDLE[count];

        verify(true, !!EnumDependentServices(svc, SERVICE_ACTIVE, depStatus, size, &size, &count));

        // open each service, get it's current status, and tell it to stop
        for (DWORD i = 0; i < count; i++)
        {
            depService[i] = OpenService(mgr, depStatus[i].lpServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);

            if (nullptr != depService[i] && SERVICE_STOPPED != depStatus[i].ServiceStatus.dwCurrentState && SERVICE_STOP_PENDING != depStatus[i].ServiceStatus.dwCurrentState)
                verify(true, !!ControlService(depService[i], SERVICE_CONTROL_STOP, &depStatus[i].ServiceStatus));
        }

        // wait for each service to halt
        for (DWORD i = 0; i < count; i++)
        {
            if (nullptr == depService[i])
                continue;

            while (SERVICE_STOPPED != depStatus[i].ServiceStatus.dwCurrentState)
            {
                Sleep(depStatus[i].ServiceStatus.dwWaitHint / 10);

                SERVICE_STATUS_PROCESS status;
                verify(true, !!QueryServiceStatusEx(depService[i], SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &size));

                if (status.dwCurrentState == SERVICE_STOPPED)
                    break;

                if (GetTickCount() - start > timeout)
                    break;
            }

            CloseServiceHandle(depService[i]);
        }

        delete[] depStatus;
        delete[] depService;

        return TRUE;
    }
} // namespace [anonymous]

namespace cc
{
    service_manager::service_manager(console& c)
        : m_console(c)
    {
    }

    service_manager::~service_manager()
    {
    }

    bool service_manager::install(char const* const serviceName,
                                  Mode const mode,
                                  char const* const friendlyName,
                                  char const* const description,
                                  char const* exePath)
    {
        (void)description;

        bool success{ false };

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE svc = OpenService(mgr, serviceName, SERVICE_ALL_ACCESS);

            // service doesn't already exist, so add it
            if (svc == nullptr)
            {
                constexpr DWORD modeMap[] =
                {
                    SERVICE_AUTO_START,   // Mode::kAuto
                    SERVICE_DEMAND_START, // Mode::kDemand (manual)
                };

                svc = CreateService(mgr,
                                    serviceName,
                                    friendlyName,
                                    SERVICE_ALL_ACCESS,
                                    SERVICE_WIN32_OWN_PROCESS,
                                    modeMap[mode],
                                    SERVICE_ERROR_NORMAL,
                                    exePath,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr);

            }

            if (nullptr == svc)
                m_console.logf(Source::kUtility, Level::kError, "Failed to install service \"%s\", GLE[0x%x]", serviceName, GetLastError());
            else
            {
                success = true;
                CloseServiceHandle(svc);
            }

            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::uninstall(char const* const serviceName)
    {
        bool success = false;

        stop(serviceName);

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, DELETE);

            if (nullptr != svc)
            {
                success = !!DeleteService(svc);
                CloseServiceHandle(svc);
            }

            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::enable(char const* const serviceName)
    {
        bool success = false;

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, SERVICE_CHANGE_CONFIG);
            if (svc != NULL)
            {
                success = ChangeServiceConfig(svc,
                                              SERVICE_NO_CHANGE,
                                              SERVICE_DEMAND_START,
                                              SERVICE_NO_CHANGE,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr);

                CloseServiceHandle(svc);
            }
            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::disable(char const* const serviceName)
    {
        bool success = false;

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, SERVICE_CHANGE_CONFIG);
            if (svc != NULL)
            {
                success = ChangeServiceConfig(svc,
                                              SERVICE_NO_CHANGE,
                                              SERVICE_DISABLED,
                                              SERVICE_NO_CHANGE,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              nullptr);

                CloseServiceHandle(svc);
            }
            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::query(char const* const serviceName)
    {
        bool success{ false };

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, SERVICE_QUERY_CONFIG);
            if (nullptr != svc)
            {
                LPQUERY_SERVICE_CONFIG lpsc{};
                LPSERVICE_DESCRIPTION lpsd{};
                DWORD size{};

                verify(false, !!QueryServiceConfig(svc, nullptr, 0, &size));
                assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
                lpsc = (LPQUERY_SERVICE_CONFIG)LocalAlloc(LMEM_FIXED, size);
                verify(true, !!QueryServiceConfig(svc, lpsc, size, &size));

                verify(false, !!QueryServiceConfig2(svc, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &size));
                assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
                lpsd = (LPSERVICE_DESCRIPTION)LocalAlloc(LMEM_FIXED, size);
                verify(true, !!QueryServiceConfig2(svc, SERVICE_CONFIG_DESCRIPTION, (LPBYTE)lpsd, size, &size));

#if 0
                _tprintf(TEXT("%s configuration: \n"), szSvcName);
                _tprintf(TEXT("  Type: 0x%x\n"), lpsc->dwServiceType);
                _tprintf(TEXT("  Start Type: 0x%x\n"), lpsc->dwStartType);
                _tprintf(TEXT("  Error Control: 0x%x\n"), lpsc->dwErrorControl);
                _tprintf(TEXT("  Binary path: %s\n"), lpsc->lpBinaryPathName);
                _tprintf(TEXT("  Account: %s\n"), lpsc->lpServiceStartName);

                if (lpsd->lpDescription != NULL && lstrcmp(lpsd->lpDescription, TEXT("")) != 0)
                    _tprintf(TEXT("  Description: %s\n"), lpsd->lpDescription);
                if (lpsc->lpLoadOrderGroup != NULL && lstrcmp(lpsc->lpLoadOrderGroup, TEXT("")) != 0)
                    _tprintf(TEXT("  Load order group: %s\n"), lpsc->lpLoadOrderGroup);
                if (lpsc->dwTagId != 0)
                    _tprintf(TEXT("  Tag ID: %d\n"), lpsc->dwTagId);
                if (lpsc->deps != NULL && lstrcmp(lpsc->deps, TEXT("")) != 0)
                    _tprintf(TEXT("  Dependencies: %s\n"), lpsc->deps);
#endif // 0

                success = true;

                LocalFree(lpsc);
                LocalFree(lpsd);

                CloseServiceHandle(svc);
            }

            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::start(char const* const serviceName)
    {
        bool success{ false };

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, SERVICE_ALL_ACCESS);
            if (nullptr == svc)
                m_console.logf(Source::kUtility, Level::kError, "Failed to open service; is it installed? GLE[0x%x]", GetLastError());
            else
            {
                DWORD start{ GetTickCount() };
                DWORD checkpoint{ MAXDWORD };

                for (;;)
                {
                    // what is the current state of things?
                    DWORD size{};
                    SERVICE_STATUS_PROCESS status{};
                    verify(true, !!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &size));

                    // started; bail
                    if (SERVICE_RUNNING == status.dwCurrentState)
                    {
                        success = true;
                        break;
                    }

                    DWORD const now = GetTickCount();

                    // unchanged checkpoint and timed out; bail
                    if (status.dwCheckPoint == checkpoint && now - start > status.dwWaitHint)
                        break;

                    start = now;
                    checkpoint = status.dwCheckPoint;

                    if (status.dwCurrentState == SERVICE_STOPPED || status.dwCurrentState == SERVICE_PAUSED)
                    {
                        if (!StartService(svc, 0, nullptr))
                        {
                            m_console.logf(Source::kUtility, Level::kError, "Unable to start service. GLE[0x%x]", GetLastError());
                            break;
                        }
                    }
                    else
                    {
                        // one of the pending states so just wait
                        Sleep(clamp(status.dwWaitHint / 10, 1000, 10000));
                    }
                }

                CloseServiceHandle(svc);
            }
            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::stop(char const* const serviceName)
    {
        bool success{ false };

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, SERVICE_ALL_ACCESS);
            if (nullptr != svc)
            {
                DWORD start{ GetTickCount() };
                DWORD checkpoint{ MAXDWORD };

                for (;;)
                {
                    // what is the current state of things?
                    DWORD size{};
                    SERVICE_STATUS_PROCESS status{};
                    verify(true, !!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &size));

                    // stopped; bail
                    if (SERVICE_STOPPED == status.dwCurrentState)
                    {
                        success = true;
                        break;
                    }

                    DWORD const now = GetTickCount();

                    // unchanged checkpoint and timed out; bail
                    if (status.dwCheckPoint == checkpoint && now - start > status.dwWaitHint)
                        break;

                    start = now;
                    checkpoint = status.dwCheckPoint;

                    switch (status.dwCurrentState)
                    {
                        // start or stop in progress; wait for it
                        case SERVICE_STOP_PENDING:
                        case SERVICE_START_PENDING:
                        case SERVICE_CONTINUE_PENDING:
                        case SERVICE_PAUSE_PENDING:
                            Sleep(clamp(status.dwWaitHint / 10, 1000, 10000));
                            break;

                        case SERVICE_RUNNING:
                        case SERVICE_PAUSED:
                        default:
                            stop_dependent_services(mgr, svc);

                            verify(true, !!ControlService(svc, SERVICE_CONTROL_STOP, reinterpret_cast<SERVICE_STATUS*>(&status)));
                            break;
                    }
                }

                CloseServiceHandle(svc);
            }
            CloseServiceHandle(mgr);
        }

        return success;
    }

    bool service_manager::restart(char const* const serviceName)
    {
        stop(serviceName);
        return start(serviceName);
    }

    bool service_manager::commit(char const* const serviceName)
    {
        bool success = false;

        SC_HANDLE const mgr = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (nullptr == mgr)
            m_console.logf(Source::kUtility, Level::kError, "Unable to open service manager (are you running as admin?) GLE[0x%x]", GetLastError());
        else
        {
            SC_HANDLE const svc = OpenService(mgr, serviceName, SERVICE_CHANGE_CONFIG);
            if (nullptr != svc)
            {
                SERVICE_DESCRIPTIONA sd;
                sd.lpDescription = (char*)"This is a test description";

                success = !ChangeServiceConfig2(svc, SERVICE_CONFIG_DESCRIPTION, &sd);

                CloseServiceHandle(svc);
            }
            CloseServiceHandle(mgr);
        }

        return success;
    }
} // namespace cc::service
