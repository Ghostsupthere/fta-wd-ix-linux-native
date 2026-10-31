#pragma once
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_set>
// Failure-only, deduplicated and capped per process. No asset/config mutation.
inline void WxlTextureFailure(const char* stage,const char* path) {
    if (!stage || !path) return;
    static std::mutex mutex;
    static std::unordered_set<std::string> seen;
    std::lock_guard<std::mutex> lock(mutex);
    const std::string line=std::string(stage)+": "+path;
    if (seen.size()>=256 || seen.count(line)) return;
    FILE* file=std::fopen("Logs/TextureFailures.log",seen.empty()?"w":"a");
    if (!file) return;
    std::fprintf(file,"%s\n",line.c_str());std::fclose(file);
    seen.insert(line);
}
