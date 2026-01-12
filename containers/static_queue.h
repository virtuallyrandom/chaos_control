#pragma once

#include <common/type_traits.h>
#include <common/types.h>
#include <containers/queue.h>

namespace cc
{
    template <typename Type, size_t Count>
    class static_queue final : public queue
    {
    public:
        static_queue() : queue(sizeof(Type), Count, buffer, sizeof(buffer)) {}
        ~static_queue() = default;

        template <class... Args>
        [[nodiscard]] Type* write_acquire(Args&&... args);
        void write_release(Type* const ptr);

        [[nodiscard]] Type* read_acquire();
        void read_release(Type* const ptr);

        template <class... Args>
        [[nodiscard]] bool push(Args&&... args);

        [[nodiscard]] bool pop(Type* const);

        virtual void clear() override;

    private:
        compiler_disable_copymove(static_queue)

        byte buffer[required_size(sizeof(Type), Count)];
    };
} // namespace cc

#include <containers/static_queue.inl>