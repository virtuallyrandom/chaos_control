#include "test.h"

#include <common/variant.h>
#include <containers/unordered_map.h>
#include <containers/vector.h>

class variantTest : public cc::test
{
public:
    variantTest() = default;

    virtual cc::string operator()() override
    {
        cc::string error;

        try
        {
            using vec_type = cc::vector<uint16_t>;
            using map_type = cc::unordered_map<uint64_t, uint64_t>;
            cc::variant<int, cc::string, vec_type, map_type> var;

            var = 1;
//            var = "this is a string";
            //            var[1] = cc::variant{ "test" };

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
} variantTest;
