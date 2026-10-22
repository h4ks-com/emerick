#include "emerick/app/path_access.hpp"

#include <algorithm>
#include <system_error>

namespace emerick::app {

namespace {

bool startsWith(const std::filesystem::path& path, const std::filesystem::path& root) {
    return std::ranges::mismatch(root, path).in1 == root.end();
}

// weakly_canonical keeps a symlink that points nowhere, and a write through it would create a file wherever it points.
bool holdsDanglingLink(std::filesystem::path path, const std::filesystem::path& root) {
    for (; path != root && path.has_relative_path(); path = path.parent_path()) {
        std::error_code error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error))) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<std::filesystem::path> pathInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    if (path.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return std::nullopt;
    }
    std::filesystem::path resolved = std::filesystem::weakly_canonical(absolute, error);
    if (error || !startsWith(resolved, root) || holdsDanglingLink(resolved, root)) {
        return std::nullopt;
    }
    return resolved;
}

}  // namespace emerick::app
