#pragma once

#include <QMetaObject>
#include <QObject>
#include <QtLogging>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/project_controller.hpp"

namespace emerick::yamaha {

// Hands every message the port receives to receive on the thread of context. A port the keyboard does not offer
// right now gives no input, and a listed port that cannot open logs why.
inline std::unique_ptr<engine::MidiInput> listenOnPort(app::ProjectController& controller, const std::string& port,
                                                       QObject& context,
                                                       std::function<void(const std::vector<std::uint8_t>&)> receive) {
    const auto& ports = controller.engine().midiPorts();
    if (std::ranges::none_of(ports,
                             [&](const engine::MidiPortInfo& info) { return info.isInput && info.id == port; })) {
        return nullptr;
    }
    try {
        return controller.engine().midi().openInput(
            port, [&context, receive = std::move(receive)](std::span<const std::uint8_t> bytes, std::int64_t) {
                QMetaObject::invokeMethod(
                    &context, [receive, message = std::vector(bytes.begin(), bytes.end())] { receive(message); },
                    Qt::QueuedConnection);
            });
    } catch (const std::invalid_argument& error) {
        qWarning("%s", error.what());
    } catch (const std::runtime_error& error) {
        qWarning("%s", error.what());
    }
    return nullptr;
}

}  // namespace emerick::yamaha
