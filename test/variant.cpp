#include "test.h"

#include <common/variant.h>
#include <containers/unordered_map.h>
#include <containers/vector.h>

class variant_test : public cc::test
{
public:
    variant_test() = default;

    virtual cc::string operator()() override
    {
        cc::string error;

        try
        {
            using vec_type = cc::vector<uint16_t>;
            using map_type = cc::unordered_map<uint64_t, uint64_t>;
            cc::variant<int, cc::string, vec_type, map_type> var("this is a string");

            cc::string to = var;

            if (var.holds<int>())
            {
                printf("int\n");
            }
            else if (var.holds<cc::string>())
            {
                printf("string via holds\n");
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
