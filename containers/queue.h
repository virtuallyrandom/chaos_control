#pragma once

#include <common/atomic.h>
#include <common/compiler.h>
#include <common/math.h>
#include <common/concurrency.h>
#include <common/types.h>

namespace cc {

    class queue
    {
    public:
        virtual ~queue() = default;

        [[nodiscard]] void* write_acquire();
        void write_release(void* const ptr);

        [[nodiscard]] void* read_acquire();
        void read_release(void* const ptr);

        virtual void clear();

        [[nodiscard]] bool isEmpty() const;

    protected:
        static constexpr size_t DEFAULT_ALIGNMENT = 16;

        static constexpr size_t required_size(size_t const elementSize, size_t elementCount)
        {
            return sizeof(impl) +
                   freelist_size(elementCount) +
                   storage_size(elementSize, elementCount) +
                   element_size(elementCount);
        }

        static constexpr size_t freelist_size(size_t const count)
        {
            return align(count * sizeof(atomic<size_t>), DEFAULT_ALIGNMENT);
        }

        static constexpr size_t storage_size(size_t const elementSize, size_t count)
        {
            return align(count * elementSize, DEFAULT_ALIGNMENT);
        }

        static constexpr size_t element_size(size_t const count)
        {
            return align(count * sizeof(atomic<size_t>), DEFAULT_ALIGNMENT);
        }

        queue(size_t const elementSize, size_t const elementCount, void* const buffer, size_t const bufferSize);

    private:
        compiler_disable_copymove(queue);

        struct impl
        {
compiler_push_disable_implicit_padding()
#pragma push_macro("ALIGN")
#define ALIGN __declspec(align(hardware_destructive_interference_size))
            ALIGN atomic<size_t>  pushIndex{};
            ALIGN atomic<size_t>  pushAvail{};
            ALIGN atomic<size_t>  popIndex{};
            ALIGN atomic<size_t>  popAvail{};
            ALIGN atomic<size_t>  freeNext{};
            ALIGN size_t          elementCount = 0;
            size_t          elementSize = 0;
            atomic<size_t>* freeList = nullptr;
            byte* storage = nullptr;
            atomic<void*>* elementList = nullptr;
            bool            isDynamic = false;
#pragma pop_macro("ALIGN")
compiler_pop_disable_implicit_padding()

            impl() = default;
            ~impl() = default;
            impl(const impl&) = delete;
            impl(impl&&) = delete;
            impl& operator=(const impl&) = delete;
            impl& operator=(impl&&) = delete;
        };

        impl* me = nullptr;
    };
} // namespace cc
