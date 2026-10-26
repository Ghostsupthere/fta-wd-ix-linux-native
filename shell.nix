# Cross-compile wowdecomp to a Windows x64 exe from NixOS.
#
# Windows PE rather than a native Linux ELF deliberately. The point of this
# build is the 64-bit address space, and a 64-bit PE under Wine has it just as
# a native binary would -- while a native port would mean rewriting the Win32
# surface this code is built on: 179 files include windows.h, 134 use HANDLE,
# and 15 use SEH, which has no Linux equivalent. x64 Windows is also the
# configuration upstream targets and tests.
#
#   nix-shell
#   ./fetch-sdk.sh        # one-off, downloads the MSVC CRT + Windows SDK
#   ./build.sh
{ pkgs ? import <nixpkgs> { } }:

let
  # 19 or newer: the MSVC STL that ships in the Windows SDK static_asserts on
  # the compiler version and rejects Clang 18 outright.
  llvm = pkgs.llvmPackages_20;
in
pkgs.mkShell {
  name = "wowdecomp-x64";

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    llvm.clang           # provides clang-cl
    llvm.bintools        # lld-link, llvm-lib, llvm-rc
    pkgs.xwin            # fetches the MSVC CRT + Windows SDK
    pkgs.cacert
    pkgs.python3         # upstream's re/tools scripts
  ];

  shellHook = ''
    export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt
    export XWIN_DIR="$PWD/.xwin"
    # clang -print-resource-dir points into the wrapper output, but
    # nixpkgs ships the intrinsic headers (emmintrin.h and friends) in
    # clang's separate lib output. Without these the MSVC CRT's
    # declaration-only versions win and every _mm_* ends up undefined
    # at link time.
    export CLANG_RESOURCE_INCLUDE=$(echo ${llvm.clang-unwrapped.lib}/lib/clang/*/include)
    # Nix's clang wrapper injects Linux sysroot flags that clang-cl must not
    # see; the unwrapped binaries are what the toolchain file looks for.
    export PATH="${llvm.clang.cc}/bin:${llvm.bintools.bintools}/bin:$PATH"
    echo "wowdecomp x64 cross-build shell"
    echo "  clang-cl: $(command -v clang-cl || echo MISSING)"
    echo "  lld-link: $(command -v lld-link || echo MISSING)"
    if [ ! -d .xwin/crt/include ]; then
      echo "  SDK:      not fetched yet -- run ./fetch-sdk.sh"
    else
      echo "  SDK:      .xwin present"
    fi
  '';
}
