#include <common/assert.h>
#include <common/platform/windows.h>
#include <common/stdlib.h>
#include <common/thread.h>
#include <utility/service.h>

#include "app.h"

#include <control-lib/control-lib.h>
#include <control-svc/control-svc.h>

#pragma comment(lib, "common.lib")
#pragma comment(lib, "containers.lib")
#pragma comment(lib, "utility.lib")

static void on_crash(void* const)
{
    MessageBoxA(NULL, "crashed?\n\nWell, that sucks!", "Oshz...", MB_OK);
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    cc::socket::initialize();

    app* the_app = new app(__argc, __argv);

    ::AllocConsole();

    cc::service_manager svc_mgr(the_app->console);

    if (the_app->args.has("svc:install"))
    {
        char path[MAX_PATH + 2];
        DWORD path_len = ::GetModuleFileNameA(nullptr, path + 1, sizeof(path) - 1);

        // surround it in quotes
        path[0] = '"';
        path[++path_len] = '"';
        path[++path_len] = 0;

        // same path as self, just change 'app.exe' to 'svc.exe'
        const DWORD path_off = path_len - 8;
        assert(strcmp(path + path_off, "app.exe\"") == 0);
        memcpy(path + path_off, "svc", 3);

        verify(true, svc_mgr.install(kServiceName, svc_mgr.kAuto, kServiceFriendlyName, kServiceDescription, path));
        return 0;
    }

    if (the_app->args.has("svc:start"))
    {
        verify(true, svc_mgr.start(kServiceName));
        return 0;
    }

    if (the_app->args.has("svc:stop"))
    {
        verify(true, svc_mgr.stop(kServiceName));
        return 0;
    }

    if (the_app->args.has("svc:restart"))
    {
        verify(true, svc_mgr.restart(kServiceName));
        return 0;
    }

    if (the_app->args.has("svc:uninstall"))
    {
        verify(true, svc_mgr.uninstall(kServiceName));
        return 0;
    }

    if (the_app->args.has("svc:disable"))
    {
        verify(true, svc_mgr.disable(kServiceName));
        return 0;
    }

    if (the_app->args.has("svc:enable"))
    {
        verify(true, svc_mgr.enable(kServiceName));
        return 0;
    }

    HMODULE module{};
    control_api api{};
    control_lib* lib{};
    if (the_app->args.has("svc:simulate"))
    {
        module = LoadLibraryA("control-lib.dll");
        if (nullptr != module)
        {
            get_api_fn getApi;
            *(void**)&getApi = GetProcAddress(module, kGetAPIName);
            if (nullptr != getApi)
            {
                getApi(&api);

                lib = api.create(__argc, __argv);

                if (!api.start(lib))
                {
                    api.destroy(lib);
                    lib = nullptr;
                }
            }
        }
    }

    // todo:
    // connect to the service
    // ? obtain credentials?
    // request connection notifications

    for (;;)
    {
        // if we are simulating the service, update it
        if (nullptr != lib)
            api.update(lib);
    }

    // if we were simulating the service, stop it now
    if (nullptr != module)
    {
        if (nullptr != lib)
        {
            api.stop(lib);
            api.destroy(lib);
        }
        FreeLibrary(module);
    }

    return 0;
}
