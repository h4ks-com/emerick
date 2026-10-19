#include "emerick/app/ui_api.hpp"

#include <chrono>

#include "emerick/app/introspector.hpp"

namespace emerick::app {

namespace {

constexpr auto kRevealTimeout = std::chrono::milliseconds{2000};
constexpr std::size_t kFindLimit = 20;

}  // namespace

Json UiApi::tree(bool namedOnly, const std::string& root) const { return uiTree(*registry_, namedOnly, qs(root)); }

Json UiApi::find(const std::string& query) const {
    Json entries = Json::array();
    for (const SearchHit& hit : registry_->search(query, kFindLimit)) {
        entries.push_back({{"kind", enumKey(hit.kind)},
                           {"id", hit.id},
                           {"title", hit.title},
                           {"category", hit.category},
                           {"location", hit.location},
                           {"score", hit.match.score}});
    }
    return {{"entries", entries}, {"items", findItems(qs(query))}};
}

Json UiApi::reveal(const std::string& id) const {
    const Revealed revealed = registry_->reveal(id);
    Json result{{"target", revealed.target}, {"chain", revealed.chain}};
    if (auto item = waitForVisibleItem(qs(revealed.target), kRevealTimeout)) {
        result["item"] = std::move(*item);
    } else {
        result["error"] = revealed.target + " is not visible after 2000 ms";
    }
    return result;
}

Json UiApi::set(const std::string& id, const Json& value, const std::optional<std::string>& property) const {
    if (registry_->setting(id) != nullptr) {
        registry_->setSetting(id, value);
        return {{"setting", registry_->describeSetting(id)}};
    }
    if (!property) {
        throw AppError(id + " is not a setting; pass property to set a UI item property");
    }
    return {{"item", setItemProperty(qs(id), qs(*property), value)}};
}

Json UiApi::invoke(const std::string& id, ActionSource source) const {
    if (registry_->action(id) != nullptr) {
        return {{"result", registry_->run(id, Json::object(), source)}};
    }
    return {{"item", invokeItem(qs(id))}};
}

Json UiApi::screenshot(const std::string& path) { return app::screenshot(qs(path)); }

}  // namespace emerick::app
