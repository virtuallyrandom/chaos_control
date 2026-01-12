#pragma once

#include <common/compiler.h>
#include <common/type_traits.h>
#include <common/types.h>
#include <containers/freelist.h>

namespace cc
{
    template <typename Type, size_t Count>
    class static_freelist : freelist
    {
    public:
        static_freelist() : freelist(sizeof(Type), Count, m_storage) {}

        template <class... Args>
        [[nodiscard]] Type* acquire(Args&&... args);

        void release(Type* const);

        virtual void clear();

    private:
        compiler_disable_copymove(static_freelist);

        decl_align(alignof(Type)) byte m_storage[freelist::required_size(sizeof(Type), Count)];
    };
} // namespace cc

#include <containers/static_freelist.inl>