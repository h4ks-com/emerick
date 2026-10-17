#pragma once

#include <optional>
#include <string>

#include "emerick/app/registry.hpp"

namespace emerick::app {

// What MCP and Lua can do with the UI, so both behave the same.
class UiApi {
public:
    explicit UiApi(Registry& registry) : registry_(&registry) {}

    [[nodiscard]] Json tree(bool namedOnly, const std::string& root) const;
    // Registry entries ranked by the fuzzy query, and live items whose objectName or text contains it.
    [[nodiscard]] Json find(const std::string& query) const;
    // Waits up to 2 s, processing events, for the revealed item to show; the result names an error otherwise.
    [[nodiscard]] Json reveal(const std::string& id) const;
    // Sets a setting by id, or the property of the UI item with that objectName.
    [[nodiscard]] Json set(const std::string& id, const Json& value, const std::optional<std::string>& property) const;
    // Runs an action by id, or clicks the UI item with that objectName.
    [[nodiscard]] Json invoke(const std::string& id) const;
    [[nodiscard]] static Json screenshot(const std::string& path);

private:
    Registry* registry_;
};

}  // namespace emerick::app
