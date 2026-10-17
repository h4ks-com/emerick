#pragma once

#include <sndfile.h>

#include <memory>

namespace emerick::engine {

struct SndFileCloser {
    void operator()(SNDFILE* file) const { sf_close(file); }
};
using SndFile = std::unique_ptr<SNDFILE, SndFileCloser>;

}  // namespace emerick::engine
