#include "test.h"

#include <common/assert.h>
#include <common/stdio.h>
#include <common/string.h>

#pragma comment(lib, "common.lib")

int main()
{
    cc::test* test = cc::g_first_test;
    while (test)
    {
        cc::string errors = (*test)();
        if (errors.empty())
            printf("%s: Success\n", test->name());
        else
            printf("%s: Failed:\n%s", test->name(), errors.c_str());
        test = test->next();
    }
}
