// Native replacement for WarcraftXL's host-side M3 drop-in provider (M3Host.cpp, GPL-3.0-or-later).
// The conversion itself (WxlM3/) is the supplied shared code, imported unchanged; this file only
// finds the sources and runs it. Differences from the host: Linux case-insensitive path lookup, a
// size cap on every read, an `m3a` option that must name a file inside the drop-in folder, and the
// conversion itself runs in a forked child with CPU and memory limits and a deadline. The shared
// MD34 reader trusts the offsets and counts in the file, so a malformed .m3 would otherwise be able
// to crash or stall the client; in the child it can only fail that one conversion.
#include "Engine/Source/ModernAssets/ModernM3.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"

#include "WxlM3/models/m3/M2Build.hpp"
#include "WxlM3/models/m3/Md34.hpp"
#include "WxlM3/models/m3/Options.hpp"
#include "WxlBlp/textures/dds/Dds.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace m3 = wxl::modern::assets::m3;
namespace dds_ = wxl::modern::assets::textures::dds;

constexpr uintmax_t kMaxSourceBytes = 256u * 1024u * 1024u;

struct State {
    std::mutex mutex;
    bool scanned = false;
    std::vector<fs::path> roots;                       // ZB first, then ZA
    std::vector<m3::ModelSource> sources;
    std::unordered_map<std::string, size_t> byName;    // served model and skin names -> source
    std::unordered_map<std::string, std::vector<uint8_t>> cache;
    std::unordered_set<std::string> miss;              // names known not to be drop-ins
    std::unordered_set<std::string> failed;            // sources whose conversion failed
    std::unordered_set<std::string> fxTextures;        // additive particle/ribbon atlases
};

State& S() {
    static State s;
    return s;
}

// -1 unscanned, 0 no loose roots, 1 present. Read without the lock: this runs for every file open.
std::atomic<int> g_looseState{-1};

bool EndsWith(std::string_view name, std::string_view suffix) {
    if (suffix.size() > name.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(name[name.size() - suffix.size() + i])) != suffix[i]) return false;
    return true;
}

bool MightServe(std::string_view name) {
    return EndsWith(name, ".m2") || EndsWith(name, ".skin") || EndsWith(name, ".blp");
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Finds `relative` (lowercase, backslash separated) under `root`, ignoring case. Names with a
// parent-directory component or an absolute start never resolve.
bool ResolveCi(const fs::path& root, const std::string& relative, fs::path& found) {
    if (relative.empty() || relative[0] == '\\' || relative[0] == '/') return false;
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= relative.size()) {
        size_t end = relative.find_first_of("\\/", start);
        if (end == std::string::npos) end = relative.size();
        std::string part = relative.substr(start, end - start);
        if (part == ".." || part.find(':') != std::string::npos) return false;
        if (!part.empty() && part != ".") parts.push_back(std::move(part));
        start = end + 1;
    }
    if (parts.empty()) return false;

    fs::path current = root;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::error_code ec;
        fs::path exact = current / parts[i];
        if (fs::exists(exact, ec)) {
            current = std::move(exact);
            continue;
        }
        bool matched = false;
        for (fs::directory_iterator it(current, ec), end; !ec && it != end; it.increment(ec)) {
            if (Lower(it->path().filename().string()) == parts[i]) {
                current = it->path();
                matched = true;
                break;
            }
        }
        if (!matched) return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(current, ec)) return false;
    found = std::move(current);
    return true;
}

bool ReadWhole(const fs::path& path, std::vector<uint8_t>& out) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size == 0 || size > kMaxSourceBytes) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.resize(static_cast<size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)));
}

std::string SkinName(const std::string& target) {
    return target.substr(0, target.size() - 3) + "00.skin";   // "<base>.m2" -> "<base>00.skin"
}

void ScanLooseRootsOnce() {
    State& s = S();
    if (s.scanned) return;
    s.scanned = true;

    std::error_code ec;
    const fs::path executable = fs::canonical("/proc/self/exe", ec);
    if (!ec) {
        const fs::path data = executable.parent_path() / "Data";
        // Only the two Modern Assets archive names may act as loose roots, in this priority order.
        for (const char* wanted : {"patch-zb.mpq", "patch-za.mpq"}) {
            std::error_code listError;
            for (fs::directory_iterator it(data, listError), end; !listError && it != end; it.increment(listError)) {
                std::error_code typeError;
                if (Lower(it->path().filename().string()) == wanted && it->is_directory(typeError)) {
                    s.roots.push_back(it->path());
                    break;
                }
            }
        }
    }
    g_looseState.store(s.roots.empty() ? 0 : 1, std::memory_order_release);
    WxlNativeDiagnostic("modern-m3: %u loose roots", unsigned(s.roots.size()));
}

// Resolves a served ".m2" name to the drop-in source beside it and registers its model and skin names.
size_t ResolveDropin(const std::string& key) {
    State& s = S();
    const auto known = s.byName.find(key);
    if (known != s.byName.end()) return known->second;
    if (s.miss.count(key)) return SIZE_MAX;

    const std::string stem = key.substr(0, key.size() - 3);
    for (const fs::path& root : s.roots) {
        fs::path modelPath;
        if (!ResolveCi(root, stem + ".m3", modelPath)) continue;

        m3::ModelSource source;
        source.target = key;
        source.m3 = modelPath.string();
        fs::path libraryPath;
        if (ResolveCi(root, stem + ".m3a", libraryPath)) source.m3a = libraryPath.string();

        const size_t separator = key.rfind('\\');
        const std::string directory = separator != std::string::npos ? key.substr(0, separator + 1) : "";
        source.tex = directory;
        source.texroot = (root / fs::path(directory)).string();

        fs::path configPath;
        std::vector<uint8_t> config;
        if (ResolveCi(root, stem + ".m3cfg", configPath) && ReadWhole(configPath, config)) {
            const std::string configuredLibrary = source.m3a;
            m3::ParseOptions(std::string(config.begin(), config.end()), source);
            // The `m3a` option names a library by path; it must stay inside this drop-in folder.
            if (source.m3a != configuredLibrary) {
                fs::path configured;
                source.m3a = ResolveCi(root, Lower(source.m3a), configured) ? configured.string() : configuredLibrary;
            }
        }

        s.sources.push_back(std::move(source));
        const size_t index = s.sources.size() - 1;
        s.byName.emplace(key, index);
        s.byName.emplace(SkinName(key), index);
        WxlNativeDiagnostic("modern-m3: drop-in %s <- %s", key.c_str(), modelPath.string().c_str());
        return index;
    }
    s.miss.insert(key);
    return SIZE_MAX;
}

size_t SourceFor(const std::string& key) {
    State& s = S();
    const auto it = s.byName.find(key);
    if (it != s.byName.end()) return it->second;
    if (key.size() > 3 && key.compare(key.size() - 3, 3, ".m2") == 0) return ResolveDropin(key);
    if (key.size() > 7 && key.compare(key.size() - 7, 7, "00.skin") == 0) {
        const size_t index = ResolveDropin(key.substr(0, key.size() - 7) + ".m2");
        return (index != SIZE_MAX && s.byName.count(key)) ? index : SIZE_MAX;
    }
    return SIZE_MAX;
}

struct Converted {
    std::vector<uint8_t> model, skin;
    std::vector<std::string> fxTextures;
};

// The conversion proper (the host's Convert): no shared state, so it can run in a child process.
bool BuildConverted(const m3::ModelSource& e, const std::vector<uint8_t>& modelBytes,
                    std::vector<uint8_t> libraryBytes, Converted& out) {
    const m3::Mat4 frame = m3::ModelFrame(e.lift);
    m3::Model model;
    if (!m3::ParseModel(modelBytes.data(), modelBytes.size(), frame, model)) {
        WxlNativeDiagnostic("modern-m3: parse failed %s", e.m3.c_str());
        return false;
    }
    if (e.ribmesh) m3::RibbonsToMesh(model, e.ribtilt, e.riblen, e.ribbasewidth);

    m3::BakeResult bake;
    bool baked = false;
    if (!libraryBytes.empty()) {
        m3::AnimLib library;
        if (m3::ParseAnimLib(std::move(libraryBytes), library))
            baked = m3::BakeSequences(model, library, m3::ParseSeqMap(e.map), frame, bake);
        else
            WxlNativeDiagnostic("modern-m3: anim library unreadable %s", e.m3a.c_str());
    }
    if (!baked) m3::StaticBake(model, bake);

    // The model's own animation tables: ambient bone loops and layer uv scrolls.
    {
        m3::AnimLib self;
        std::vector<uint8_t> selfBytes = modelBytes;
        if (m3::ParseAnimLib(std::move(selfBytes), self)) {
            for (size_t mi = 0; mi < model.materials.size(); ++mi) {
                auto& material = model.materials[mi];
                if (!material.uvAnim) continue;
                for (uint32_t si = 0; si < self.stcs.size(); ++si) {
                    m3::LibTrack track;
                    if (!self.Track(si, material.uvAnim, 1, 8, track) || track.count < 2) continue;
                    const float duration = float(track.times[track.count - 1] - track.times[0]);
                    if (duration <= 0) continue;
                    float first[2], last[2];
                    std::memcpy(first, track.values, 8);
                    std::memcpy(last, track.values + (track.count - 1) * 8, 8);
                    material.uvRate[0] = (last[0] - first[0]) / duration;
                    material.uvRate[1] = (last[1] - first[1]) / duration;
                    // Sheet meshes run U along the length while the source ribbon scrolls its
                    // along-axis on V: flow follows the wing, negated so it runs base -> tip.
                    if (material.sheetUv) {
                        std::swap(material.uvRate[0], material.uvRate[1]);
                        material.uvRate[0] = -material.uvRate[0];
                    }
                    WxlNativeDiagnostic("modern-m3: uv scroll mat%u %.4f,%.4f /s", unsigned(mi),
                                        material.uvRate[0] * 1000.0f, material.uvRate[1] * 1000.0f);
                    break;
                }
            }
            if (!e.ambient.empty() && m3::OverlayAmbient(model, self, e.ambient, frame, bake))
                WxlNativeDiagnostic("modern-m3: ambient '%s' overlaid", e.ambient.c_str());
        }
    }

    std::string internal = e.target;
    const size_t separator = internal.rfind('\\');
    if (separator != std::string::npos) internal = internal.substr(separator + 1);
    internal = internal.substr(0, internal.size() - 3);

    m3::BuildM2(model, internal, e.tex, bake, e.parcolor, out.model, &out.fxTextures);
    m3::BuildSkin(model, out.skin);
    WxlNativeDiagnostic("modern-m3: %s verts=%u bones=%u seqs=%u emitters=%u ribbons=%u", e.target.c_str(),
                        unsigned(model.verts.size()), unsigned(model.bones.size()), unsigned(bake.seqs.size()),
                        unsigned(model.particles.size()), unsigned(model.ribbons.size()));
    return !out.model.empty() && !out.skin.empty();
}

// ---- child-process transport: [u64 model][u64 skin][u32 fx count]([u32 len][bytes])*  model  skin
bool WriteAll(int fd, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    while (size) {
        const ssize_t n = ::write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool Serialize(const Converted& c, std::vector<uint8_t>& frame) {
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        frame.insert(frame.end(), b, b + n);
    };
    const uint64_t modelSize = c.model.size(), skinSize = c.skin.size();
    const uint32_t count = static_cast<uint32_t>(c.fxTextures.size());
    put(&modelSize, 8);
    put(&skinSize, 8);
    put(&count, 4);
    for (const std::string& fx : c.fxTextures) {
        const uint32_t length = static_cast<uint32_t>(fx.size());
        put(&length, 4);
        put(fx.data(), fx.size());
    }
    put(c.model.data(), c.model.size());
    put(c.skin.data(), c.skin.size());
    return true;
}

bool Deserialize(const std::vector<uint8_t>& frame, Converted& c) {
    size_t at = 0;
    auto take = [&](void* p, size_t n) {
        if (n > frame.size() - at) return false;
        std::memcpy(p, frame.data() + at, n);
        at += n;
        return true;
    };
    uint64_t modelSize = 0, skinSize = 0;
    uint32_t count = 0;
    if (!take(&modelSize, 8) || !take(&skinSize, 8) || !take(&count, 4) || count > 4096) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t length = 0;
        if (!take(&length, 4) || length > frame.size() - at) return false;
        c.fxTextures.emplace_back(reinterpret_cast<const char*>(frame.data() + at), length);
        at += length;
    }
    if (modelSize > frame.size() - at) return false;
    c.model.assign(frame.begin() + at, frame.begin() + at + modelSize);
    at += modelSize;
    if (skinSize != frame.size() - at) return false;
    c.skin.assign(frame.begin() + at, frame.end());
    return !c.model.empty() && !c.skin.empty();
}

constexpr auto kConvertDeadline = std::chrono::seconds(20);
constexpr size_t kMaxFrameBytes = 512u * 1024u * 1024u;

// Runs BuildConverted in a forked child. The child gets a CPU limit and an address-space limit
// of current use plus 2 GiB (not under ASan, which reserves far more), and is killed at the deadline.
bool ConvertInChild(const m3::ModelSource& e, const std::vector<uint8_t>& modelBytes,
                    const std::vector<uint8_t>& libraryBytes, Converted& out) {
    int fds[2];
    if (::pipe(fds) != 0) return false;

    rlimit addressLimit{};
    bool limitMemory = false;
#if !defined(__SANITIZE_ADDRESS__)
    if (FILE* statm = std::fopen("/proc/self/statm", "r")) {
        unsigned long pages = 0;
        if (std::fscanf(statm, "%lu", &pages) == 1) {
            addressLimit.rlim_cur = addressLimit.rlim_max =
                static_cast<rlim_t>(pages) * static_cast<rlim_t>(::sysconf(_SC_PAGESIZE)) + (2ull << 30);
            limitMemory = true;
        }
        std::fclose(statm);
    }
#endif

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    if (child == 0) {
        ::close(fds[0]);
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        const rlimit cpu{static_cast<rlim_t>(kConvertDeadline.count()), static_cast<rlim_t>(kConvertDeadline.count()) + 5};
        ::setrlimit(RLIMIT_CPU, &cpu);
        if (limitMemory) ::setrlimit(RLIMIT_AS, &addressLimit);
        int status = 1;
        try {
            Converted result;
            if (BuildConverted(e, modelBytes, libraryBytes, result)) {
                std::vector<uint8_t> frame;
                Serialize(result, frame);
                status = WriteAll(fds[1], frame.data(), frame.size()) ? 0 : 1;
            }
        } catch (...) {
            status = 1;
        }
        ::_exit(status);
    }

    ::close(fds[1]);
    std::vector<uint8_t> frame;
    bool ok = true;
    const auto deadline = std::chrono::steady_clock::now() + kConvertDeadline;
    uint8_t buffer[1 << 16];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) { ok = false; break; }
        pollfd waiting{fds[0], POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(left.count()));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { ok = false; break; }
        const ssize_t n = ::read(fds[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        if (frame.size() + static_cast<size_t>(n) > kMaxFrameBytes) { ok = false; break; }
        frame.insert(frame.end(), buffer, buffer + n);
    }
    ::close(fds[0]);
    if (!ok) ::kill(child, SIGKILL);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (!ok || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        WxlNativeDiagnostic("modern-m3: conversion of %s failed in the worker (%s)", e.m3.c_str(),
                            ok ? "exit status" : "deadline or transport");
        return false;
    }
    return Deserialize(frame, out);
}

bool Convert(const m3::ModelSource& e) {
    State& s = S();
    if (s.failed.count(e.target)) return false;
    std::vector<uint8_t> modelBytes, libraryBytes;
    bool ok = ReadWhole(e.m3, modelBytes);
    if (!ok) WxlNativeDiagnostic("modern-m3: cannot read %s", e.m3.c_str());
    // An unreadable animation library bakes the model statically, as the host did.
    if (ok && !e.m3a.empty() && !ReadWhole(e.m3a, libraryBytes)) {
        WxlNativeDiagnostic("modern-m3: anim library unreadable %s", e.m3a.c_str());
        libraryBytes.clear();
    }
    Converted converted;
    if (ok) ok = ConvertInChild(e, modelBytes, libraryBytes, converted);
    if (ok) {
        s.cache[e.target] = std::move(converted.model);
        s.cache[SkinName(e.target)] = std::move(converted.skin);
        for (const std::string& texture : converted.fxTextures) s.fxTextures.insert(texture);
    } else {
        s.failed.insert(e.target);   // drop-ins are static for the session: do not retry every open
    }
    return ok;
}

// A served ".blp" name -> the ".dds" of the same name in a loose root.
bool TextureSource(const std::string& name, fs::path& found) {
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".blp") != 0) return false;
    const std::string relative = name.substr(0, name.size() - 4) + ".dds";
    for (const fs::path& root : S().roots)
        if (ResolveCi(root, relative, found)) return true;
    return false;
}
}  // namespace

bool ModernM3Provide(const char* name, std::vector<uint8_t>& output) {
    if (!name || !MightServe(name)) return false;
    if (g_looseState.load(std::memory_order_acquire) == 0) return false;
    static const bool disabled = [] {
        const char* value = std::getenv("WXL_MODERN_ASSETS");
        return value && value[0] == '0';
    }();
    if (disabled) return false;

    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    ScanLooseRootsOnce();
    if (s.roots.empty()) return false;

    const std::string key = m3::NormalizePath(name);
    if (s.miss.count(key)) return false;
    const auto cached = s.cache.find(key);
    if (cached != s.cache.end()) {
        output = cached->second;
        return true;
    }

    const size_t index = SourceFor(key);
    if (index != SIZE_MAX) {
        if (!Convert(s.sources[index])) return false;
        const auto hit = s.cache.find(key);
        if (hit == s.cache.end()) return false;
        output = hit->second;
        return true;
    }

    fs::path texturePath;
    if (TextureSource(key, texturePath)) {
        std::vector<uint8_t> raw;
        const bool luminanceAlpha = s.fxTextures.count(key) != 0;
        std::vector<uint8_t> blp;
        if (ReadWhole(texturePath, raw) && dds_::DdsToBlp(raw.data(), raw.size(), blp, luminanceAlpha)) {
            s.cache[key] = blp;
            output = std::move(blp);
            WxlNativeDiagnostic("modern-m3: texture %s <- %s", key.c_str(), texturePath.string().c_str());
            return true;
        }
    }
    s.miss.insert(key);
    return false;
}

bool ModernM3Exists(const char* name) {
    if (!name || !MightServe(name)) return false;
    if (g_looseState.load(std::memory_order_acquire) == 0) return false;

    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    ScanLooseRootsOnce();
    if (s.roots.empty()) return false;

    const std::string key = m3::NormalizePath(name);
    if (s.miss.count(key)) return false;
    if (s.cache.count(key) || SourceFor(key) != SIZE_MAX) return true;
    fs::path texturePath;
    if (TextureSource(key, texturePath)) return true;
    s.miss.insert(key);
    return false;
}
