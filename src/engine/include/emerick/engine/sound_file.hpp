#pragma once

#include <sndfile.h>

#include <filesystem>
#include <memory>

namespace emerick::engine {

struct SndFileCloser {
    void operator()(SNDFILE* file) const { sf_close(file); }
};
using SndFile = std::unique_ptr<SNDFILE, SndFileCloser>;

// Windows paths are wide strings, and only sf_wchar_open opens a file whose name the ANSI code page cannot spell.
inline SNDFILE* openSoundFile(const std::filesystem::path& path, int mode, SF_INFO* info) {
#ifdef _WIN32
    return sf_wchar_open(path.c_str(), mode, info);
#else
    return sf_open(path.c_str(), mode, info);
#endif
}

}  // namespace emerick::engine
