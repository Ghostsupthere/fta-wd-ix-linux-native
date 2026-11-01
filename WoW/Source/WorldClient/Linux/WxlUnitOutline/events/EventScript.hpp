// Native stand-in for the core's EventScript base: member-function subscriptions with an optional
// priority (lower runs first), removed again when the owner is destroyed.
#pragma once

#include <algorithm>
#include <vector>

#include "Event.hpp"

namespace wxl::events {

namespace detail {
struct Subscription {
    Event event;
    int priority;
    long order;
    const void* owner;
    void (*invoke)(void* owner, const void* args);
    void* self;
};
inline std::vector<Subscription>& Subscriptions() {
    static std::vector<Subscription> list;
    return list;
}
template <class T> struct HandlerTraits;
template <class C, class A> struct HandlerTraits<void (C::*)(const A&)> {
    using Class = C;
    using Args = A;
};
}  // namespace detail

class EventScript {
public:
    EventScript() = default;
    EventScript(const EventScript&) = delete;
    EventScript& operator=(const EventScript&) = delete;
    virtual ~EventScript() {
        auto& list = detail::Subscriptions();
        list.erase(std::remove_if(list.begin(), list.end(), [this](const detail::Subscription& s) { return s.owner == this; }),
                   list.end());
    }

protected:
    template <auto Handler>
    void on(Event event, int priority = 0) {
        using Traits = detail::HandlerTraits<decltype(Handler)>;
        static long counter = 0;
        auto& list = detail::Subscriptions();
        list.push_back({event, priority, counter++, this,
                        [](void* self, const void* args) {
                            (static_cast<typename Traits::Class*>(static_cast<EventScript*>(self))->*Handler)(
                                *static_cast<const typename Traits::Args*>(args));
                        },
                        this});
        std::stable_sort(list.begin(), list.end(), [](const detail::Subscription& a, const detail::Subscription& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.order < b.order;
        });
    }
};

// Calls every subscriber of `event`, in priority order. Called for every M2 batch, so it copies one record at
// a time rather than the whole list; a handler that subscribes or unsubscribes only affects later calls.
template <class Args>
inline void Dispatch(Event event, const Args& args) {
    auto& list = detail::Subscriptions();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].event != event) continue;
        const detail::Subscription s = list[i];
        s.invoke(s.self, &args);
    }
}

}  // namespace wxl::events
