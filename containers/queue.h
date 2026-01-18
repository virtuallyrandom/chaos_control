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

        [[nodiscard]] bool empty() const;

    protected:
        static constexpr size_t DEFAULT_ALIGNMENT = 16;

        static constexpr size_t required_size(size_t const elem_size, size_t elem_count)
        {
            return sizeof(impl) +
                   freelist_size(elem_count) +
                   storage_size(elem_size, elem_count) +
                   element_size(elem_count);
        }

        static constexpr size_t freelist_size(size_t const count)
        {
            return align(count * sizeof(atomic<size_t>), DEFAULT_ALIGNMENT);
        }

        static constexpr size_t storage_size(size_t const m_element_size, size_t count)
        {
            return align(count * m_element_size, DEFAULT_ALIGNMENT);
        }

        static constexpr size_t element_size(size_t const count)
        {
            return align(count * sizeof(atomic<size_t>), DEFAULT_ALIGNMENT);
        }

        queue(size_t const element_size, size_t const element_count, void* const buffer, size_t const bufferSize);

    private:
        compiler_disable_copymove(queue);

        struct impl
        {
compiler_push_disable_implicit_padding()
#pragma push_macro("ALIGN")
#define ALIGN __declspec(align(hardware_destructive_interference_size))
            ALIGN atomic<size_t>  m_push_index{};
            ALIGN atomic<size_t>  m_push_available{};
            ALIGN atomic<size_t>  m_pop_index{};
            ALIGN atomic<size_t>  m_pop_available{};
            ALIGN atomic<size_t>  m_free_next{};
            ALIGN size_t          m_element_count = 0;
            size_t                m_element_size = 0;
            atomic<size_t>*       m_free_list = nullptr;
            byte*                 m_storage = nullptr;
            atomic<void*>*        m_element_list = nullptr;
            bool                  m_dynamic = false;
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
