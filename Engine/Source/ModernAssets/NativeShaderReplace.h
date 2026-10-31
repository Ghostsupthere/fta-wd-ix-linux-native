#pragma once
// Every module that substitutes offline-patched shader bytecode (grass motion, surface lights)
// registers one function here. CGxShader::Load asks them in registration order through the single
// NativeVisualHooks::replaceShaderCode hook; the first one that recognises the engine's own bytecode
// supplies the replacement. A module returns false for anything it does not own, so stock shaders
// stay stock.
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace NativeShaderReplace {
using Replacer = bool (*)(const uint8_t* code, size_t size, const uint32_t** out, size_t* outBytes);

inline std::vector<Replacer>& Chain() {
    static std::vector<Replacer> chain;
    return chain;
}

inline bool Dispatch(const uint8_t* code, size_t size, const uint32_t** out, size_t* outBytes) {
    for (Replacer replacer : Chain())
        if (replacer(code, size, out, outBytes)) return true;
    return false;
}

inline void Add(Replacer replacer) {
    auto& chain = Chain();
    for (Replacer existing : chain)
        if (existing == replacer) return;
    chain.push_back(replacer);
    NativeVisualHooks::replaceShaderCode = &Dispatch;
}
}  // namespace NativeShaderReplace
