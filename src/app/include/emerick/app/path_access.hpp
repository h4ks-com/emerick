#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace emerick::app {

enum class PathAccess : std::uint8_t { Read, Write };

// The folders MCP and Lua requests may read from and write to; a missing root leaves that access open.
struct AgentRoots {
    std::optional<std::filesystem::path> read;
    std::optional<std::filesystem::path> write;
};

// The absolute form of path with every symlink resolved and every ".." applied, or nothing when that lies outside the
// canonical root or ends in a symlink that points nowhere yet.
std::optional<std::filesystem::path> pathInside(const std::filesystem::path& path, const std::filesystem::path& root);

}  // namespace emerick::app
