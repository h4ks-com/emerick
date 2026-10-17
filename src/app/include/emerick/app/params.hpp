#pragma once

#include <optional>
#include <string>
#include <utility>

#include "emerick/app/registry.hpp"

namespace emerick::app {

// Action params and tool arguments come in as JSON; these read them once into types and throw AppError naming the
// param.
template <class T>
std::optional<T> optionalParam(const Json& params, const std::string& key) {
    if (!params.is_object()) {
        throw AppError("params must be an object, got " + params.dump());
    }
    const auto found = params.find(key);
    if (found == params.end() || found->is_null()) {
        return std::nullopt;
    }
    try {
        return found->get<T>();
    } catch (const Json::exception&) {
        throw AppError("param " + key + " has the wrong type: " + found->dump());
    }
}

template <class T>
T param(const Json& params, const std::string& key) {
    if (auto value = optionalParam<T>(params, key)) {
        return std::move(*value);
    }
    throw AppError("missing param " + key);
}

}  // namespace emerick::app
