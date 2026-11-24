#include "WoW/Source/Net/NetClient/NetworkPollProfile.h"

#include <cassert>
#include <string>

using namespace NetworkPollProfile;

std::string ReadLog(FILE* log) {
    std::rewind(log);
    std::string result;
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), log)) result += buffer;
    return result;
}

int main() {
    const unsigned char packet[] = {0x99, 0xF6, 0x01};
    assert(PacketKey(packet + 1, 2) == 0x01F6);
    assert(PacketKey(packet + 1, 1) == ShortPacket);
    assert(PacketKey(nullptr, 2) == ShortPacket);

    FILE* log = std::tmpfile();
    assert(log);
    Sample sample(42);
    sample.Record(0x1F6, 7'000'000);
    sample.Record(0x1F6, 4'000'000);
    sample.Record(EventBase + 25, 2'000'000);
    sample.Record(Idle, 1'000'000);
    sample.Record(ShortPacket, 0);
    sample.Log(log, 9'999'999);
    assert(std::ftell(log) == 0);
    sample.Log(log, 20'000'000);
    const std::string summary = ReadLog(log);
    assert(summary.find("frame=42 poll=20.000 ms callbacks=14.000 ms count=5 outside=6.000 ms") != std::string::npos);
    assert(summary.find("opcode=0x01f6 count=2 ms=11.000") != std::string::npos);
    assert(summary.find("event=25 count=1 ms=2.000") != std::string::npos);
    assert(summary.find("idle count=1 ms=1.000") != std::string::npos);
    assert(summary.find("short-packet count=1 ms=0.000") != std::string::npos);
    std::fclose(log);

    log = std::tmpfile();
    assert(log);
    Sample overflow(43);
    for (uint32_t key = 0; key < 66; ++key) overflow.Record(key, 250'000);
    overflow.Record(0, 500'000);
    overflow.Log(log, 25'000'000);
    const std::string bounded = ReadLog(log);
    assert(bounded.find("callbacks=17.000 ms count=67 outside=8.000 ms") != std::string::npos);
    assert(bounded.find("opcode=0x0000 count=2 ms=0.750") != std::string::npos);
    assert(bounded.find("other count=2 ms=0.500") != std::string::npos);
    std::fclose(log);
    std::puts("network poll profile fixtures passed");
}
