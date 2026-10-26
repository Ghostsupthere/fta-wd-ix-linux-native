# Cross-building the x64 client on NixOS

Upstream expects Windows + Visual Studio. This builds the same x64 target from
Linux with clang-cl against an xwin-provided MSVC CRT and Windows SDK.

```sh
nix-shell
./fetch-sdk.sh      # one-off, ~133MB
./build.sh          # -> bin/Win/WowClient64.exe
```

Output verified as a real 64-bit PE: machine `0x8664`, optional header magic
`0x020b` (PE32+).

## Why a Windows PE and not a native Linux binary

The point of this build is the 64-bit address space, and a 64-bit PE under Wine
has that just as a native ELF would. A native port would mean rewriting the
Win32 surface the code is built on -- 179 files include `windows.h`, 134 use
`HANDLE`, 15 use SEH, which has no Linux equivalent -- across ~795k lines.
DXVK-native would cover the renderer and nothing else. x64 Windows is also the
configuration upstream targets and tests.

## What the cross-build needed

Toolchain accommodations (`cmake/clang-cl-x64.cmake`), none of which are source
bugs -- upstream builds clean under MSVC v143:

| Problem | Fix |
|---|---|
| MSVC STL `static_assert`s on compiler version | clang 19+ (shell.nix pins 20) |
| libjpeg `.c` built as C++ uses `register` | `-Wno-register` |
| Narrowing conversions in initializer lists | `-Wno-c++11-narrowing` |
| UCRT `offsetof` uses `reinterpret_cast`, so it is not a constant expression, but the engine uses it as a template argument (`TSExplicitList<T, offsetof(T, m_link)>`) | `-D_CRT_USE_BUILTIN_OFFSETOF` |
| MSVC CRT declares SSE intrinsics as plain `extern` functions (MSVC implements them in the compiler), so every `_mm_*` is undefined at link | put clang's own resource headers first. nixpkgs ships them in clang's **lib** output, which `-print-resource-dir` does not point at, so `shell.nix` exports `CLANG_RESOURCE_INCLUDE` |

Three source changes were needed, all of which preserve the original
behaviour:

- `WoW/Source/Battlenet/BSN/BSNEncodeInterface.cpp` -- 30 sites cast a
  `const void*` to a function pointer with `reinterpret_cast`, which casts away
  constness. Wrapped in `const_cast<void*>`, which is what MSVC does
  implicitly. Candidates for upstreaming.
- `WoW/Source/Ui/MinimapFrame.cpp` -- a file-local `static int32_t s_mapId`
  collides at link with the real global in `Map.cpp`, because `DayNight.h`
  declares `extern int32_t s_mapId` and reaches this file transitively. A
  `static` definition after a visible `extern` is ill-formed: MSVC keeps it
  internal (two separate variables, as the original binary had), clang gives it
  external linkage. Renamed to `s_minimapMapId`, preserving MSVC's behaviour.
- `Engine/Source/Base/OsCrt.h` -- the `RAW_TYPE_NAME` shim that lets the
  non-MSVC build fall back to `typeid(T).name()` left its `_MSC_VER` arm
  defined as itself, so every expansion named a function that does not exist.
  Only the Windows path instantiated it, so the native build never noticed.
  Restored to `typeid(T).raw_name()`, which is what upstream writes inline.

## Running

Per upstream: put the 3.3.5a `Data` folder in `bin/Win/`, point
`Data/<locale>/realmlist.wtf` at your server, and run `WowClient64.exe`.
Under Wine, use the same prefix you run the stock client in.
