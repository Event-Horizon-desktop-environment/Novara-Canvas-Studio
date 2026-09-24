#pragma once

#if defined(__has_include)
#if __has_include(<nvtx3/nvToolsExt.h>)
#include <nvtx3/nvToolsExt.h>
#define CANVAS_HAVE_NVTX 1
#endif
#endif

namespace canvas::core::nvtx {

class ScopedRange {
public:
    explicit ScopedRange(const char* name) noexcept {
#ifdef CANVAS_HAVE_NVTX
        nvtxRangePushA(name);
#else
        (void)name;
#endif
    }
    ~ScopedRange() {
#ifdef CANVAS_HAVE_NVTX
        nvtxRangePop();
#endif
    }
    ScopedRange(const ScopedRange&) = delete;
    ScopedRange& operator=(const ScopedRange&) = delete;
};

}
