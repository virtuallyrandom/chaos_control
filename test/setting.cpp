#include "test.h"

#include <utility/setting.h>

class test_setting : public cc::test
{
public:
    test_setting() = default;

    virtual cc::string operator()() override
    {
        cc::string error;

        try
        {
            {
                cc::setting stg;
                stg["one"] = "wan";
                stg["two"] = "too";
                const cc::string& value = stg["one"];
                printf("%s\n", value.c_str());
            }

            {
                cc::setting stg1;
                cc::setting stg2;
                stg1["one"] = "wan";
                stg2["two"] = "too";
                stg1 = stg2;
                const cc::string& value = stg1["two"];
                printf("%s\n", value.c_str());
            }
        }
        catch (...)
        {
        }
        return error;
    }

    virtual const char* name() const override
    {
        return "variant";
    }
} variant_test;
