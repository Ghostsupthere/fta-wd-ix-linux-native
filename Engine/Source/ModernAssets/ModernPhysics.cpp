#include "Engine/Source/ModernAssets/ModernPhysics.h"
#include <cstring>

ModernPhysicsLoadResult LoadModernPhysicsAsset(const char* modelPath, ModernPhysicsReader reader,
    ModernPhysicsResolver resolver, wow::ec::physics::Model& output,
    std::string& error, uint32_t& pendingFileId) {
    using Result = ModernPhysicsLoadResult;
    namespace phys = wxl::scripts::itemphysics;
    error.clear();
    pendingFileId = 0;
    char normalized[1024];
    if (!modelPath || !wow::ec::physics::NormPath(modelPath, normalized, sizeof(normalized))) {
        error = "invalid equipment model path";
        return Result::Rejected;
    }
    if (!std::string_view(normalized).starts_with("item\\")) return Result::Missing;
    if (!reader) { error = "missing original-asset reader"; return Result::Rejected; }
    std::vector<uint8_t> bytes;
    for (const auto& path : phys::CompanionPaths(normalized)) {
        bytes.clear();
        if (reader(path.c_str(), bytes))
            return phys::Decode(bytes, output, error) ? Result::Loaded : Result::Rejected;
    }
    bytes.clear();
    if (!reader(normalized, bytes)) return Result::Missing;
    if (bytes.size() < 4 || std::memcmp(bytes.data(), "MD21", 4)) return Result::Missing;
    if (bytes.size() > 64u * 1024u * 1024u) {
        error = "MD21 physics source exceeds 64 MiB";
        return Result::Rejected;
    }
    uint32_t fileId = 0;
    for (size_t at = 0; at < bytes.size();) {
        if (bytes.size() - at < 8) { error = "truncated MD21 physics chunk header"; return Result::Rejected; }
        uint32_t size;
        std::memcpy(&size, bytes.data() + at + 4, sizeof(size));
        if (size > bytes.size() - at - 8) { error = "truncated MD21 physics chunk"; return Result::Rejected; }
        if (!std::memcmp(bytes.data() + at, "PFDC", 4)) {
            const auto payload = std::span<const uint8_t>(bytes).subspan(at + 8, size);
            return phys::Decode(payload, output, error) ? Result::Loaded : Result::Rejected;
        }
        if (!std::memcmp(bytes.data() + at, "PFID", 4)) {
            if (size != 4) { error = "invalid PFID size"; return Result::Rejected; }
            std::memcpy(&fileId, bytes.data() + at + 8, sizeof(fileId));
        }
        at += 8 + size;
    }
    if (!fileId) return Result::Missing;
    std::string path;
    if (!resolver || !resolver(fileId, path)) {
        pendingFileId = fileId;
        return Result::PendingFileDataId;
    }
    bytes.clear();
    if (!reader(path.c_str(), bytes)) {
        error = "resolved PHYS asset could not be read";
        return Result::Rejected;
    }
    return phys::Decode(bytes, output, error) ? Result::Loaded : Result::Rejected;
}
