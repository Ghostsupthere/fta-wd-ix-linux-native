#if defined(WOWDECOMP_UNIX)

#include <fenv.h>
#include <ucontext.h>
#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

static ucontext_t s_mainContext;
static ucontext_t s_fiberContext;
static int32_t s_fiberResult = -1;

static bool ReturnFalse() {
    return false;
}

static int32_t ClientMain() {
    feclearexcept(FE_ALL_EXCEPT);
    fedisableexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
    ReturnFalse();

    return CommonMain();
}

static void MainFiberProc() {
    s_fiberResult = ClientMain();
}

int main() {
    // dxvk-native compiles in several WSI backends and throws off-Windows if it
    // has to guess which to use (wsi_platform.cpp). Select SDL3 -- the one this
    // build links -- before anything loads libdxvk_d3d9.so, so the bare binary
    // runs with no launcher. setenv's overwrite=0 leaves an explicit override
    // from the environment intact.
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);

    StormRtlInitialize();

    // TEMPORARY, for an ASan investigation session: ASan's stack-bounds/
    // use-after-return tracking assumes one real stack per OS thread, which
    // this fiber switch (same thread, two stacks via makecontext/
    // swapcontext) violates. Bypass the fiber entirely under ASan and run
    // ClientMain() directly on main()'s own stack. Remove once the
    // investigation concludes.
#ifdef __SANITIZE_ADDRESS__
    return ClientMain();
#endif

    constexpr size_t stackSize = 0x800000;
    void* stack = malloc(stackSize);

    if (!stack || getcontext(&s_fiberContext)) {
        return -1;
    }

    s_fiberContext.uc_stack.ss_sp = stack;
    s_fiberContext.uc_stack.ss_size = stackSize;
    s_fiberContext.uc_link = &s_mainContext;
    makecontext(&s_fiberContext, MainFiberProc, 0);

    if (swapcontext(&s_mainContext, &s_fiberContext)) {
        free(stack);
        return -1;
    }

    free(stack);

    return s_fiberResult;
}

#endif
