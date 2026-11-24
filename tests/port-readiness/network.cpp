#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>
#include <thread>
#include <vector>

enum EVENTID {
    EVENT_ID_NET_DATA, EVENT_ID_NET_CONNECT, EVENT_ID_NET_DISCONNECT,
    EVENT_ID_NET_CANTCONNECT, EVENT_ID_NET_DESTROY, EVENT_ID_NET_AUTH_CHALLENGE,
};
struct AuthenticationChallenge {};
struct NETEVENTQUEUENODE {
    EVENTID m_eventId;
    uint32_t m_timeReceived;
    void* m_data;
    uint32_t m_dataSize;
};
struct CriticalSection {
    bool locked = false;
    void Enter() { assert(!locked); locked = true; }
    void Leave() { assert(locked); locked = false; }
};
template<class T> struct TSList {
    std::list<T*> nodes;
    ~TSList() { assert(nodes.empty()); }
    T* Head() { return nodes.empty() ? nullptr : nodes.front(); }
    T* NewNode(int,int,int) { auto* node = new T{}; nodes.push_back(node); return node; }
    bool IsEmpty() const { return nodes.empty(); }
    void Combine(TSList* other, int mode, void*) {
        nodes.splice(mode == 1 ? nodes.begin() : nodes.end(), other->nodes);
    }
    void DeleteNode(T* node) { nodes.remove(node); delete node; }
    void Clear() { while (auto* node = Head()) DeleteNode(node); }
};
struct NetClient {
    bool m_deleteMe = false;
    struct ReferenceCount {
        unsigned n = 1;
        void Increment() { ++n; }
        unsigned Decrement() { assert(n); return --n; }
    } m_refCount;
    std::function<void(int)> callback;
    void HandleData(uint32_t, void* data, uint32_t size) {
        assert(size == sizeof(int));
        callback(*static_cast<int*>(data));
    }
    void HandleConnect() {}
    void HandleDisconnect() {}
    void HandleCantConnect() {}
    void HandleAuthChallenge(AuthenticationChallenge*) {}
    void HandleIdle() {}
};
class NETEVENTQUEUE {
public:
    explicit NETEVENTQUEUE(NetClient* client) : m_client(client) {}
    ~NETEVENTQUEUE();
    void Clear();
    void Retire();
    void PollBatch();
    void AddEvent(EVENTID, void*, NetClient*, const void*, uint32_t);
    bool Empty() const { return m_eventQueue.IsEmpty(); }
private:
    NetClient* m_client;
    CriticalSection m_critSect;
    TSList<NETEVENTQUEUENODE> m_eventQueue;
    void FinishPoll(TSList<NETEVENTQUEUENODE>&);
    void ClearBatchLocked();
    void ReleaseNode(TSList<NETEVENTQUEUENODE>&, NETEVENTQUEUENODE*);
    std::atomic<uint64_t> m_generation{0};
    bool m_polling = false;
    bool m_retired = false;
};
bool NetBatchPollEnabled() { return true; }
unsigned outstandingPayloads = 0;
bool failAllocation = false;
void SMemFree(void* ptr, const char*, int, int) { --outstandingPayloads; std::free(ptr); }
void* SMemAlloc(unsigned size, const char*, int, int) {
    if (failAllocation) return nullptr;
    ++outstandingPayloads;
    return std::malloc(size);
}
uint32_t OsGetAsyncTimeMs() { return 0; }
#include "network.inc"

int main() {
    NetClient client;
    auto* queue = new NETEVENTQUEUE(&client);
    std::vector<int> delivered;
    const auto push = [&](int value) { queue->AddEvent(EVENT_ID_NET_DATA, nullptr, &client, &value, sizeof(value)); };
    client.callback = [&](int value) {
        delivered.push_back(value);
        if (value == 1) {
            push(3);
            queue->PollBatch(); // recursive dispatch is inert
            std::this_thread::sleep_for(std::chrono::milliseconds(6));
        }
    };
    failAllocation = true;
    push(99);
    failAllocation = false;
    assert(queue->Empty() && client.m_refCount.n == 1);
    push(1);
    push(2);
    queue->PollBatch();
    assert((delivered == std::vector<int>{1}));
    queue->PollBatch();
    assert((delivered == std::vector<int>{1, 2, 3}));
    assert(queue->Empty() && client.m_refCount.n == 1 && outstandingPayloads == 0);
    client.callback = [&](int value) {
        delivered.push_back(value);
        queue->Clear();
        push(6);
    };
    push(4);
    push(5);
    queue->PollBatch();
    assert(delivered.back() == 4 && !queue->Empty());
    client.callback = [&](int value) {
        delivered.push_back(value);
        queue->Retire(); // queue destruction is deferred until the callback ends
    };
    queue->PollBatch();
    assert(delivered.back() == 6 && client.m_refCount.n == 1 && outstandingPayloads == 0);
}
