#pragma once

#include <QHttpServer>
#include <QObject>
#include <array>
#include <optional>
#include <vector>

#include "emerick/app/mcp_tools.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

inline constexpr std::array kMcpProtocolVersions{"2025-11-25", "2025-06-18", "2025-03-26"};

class McpServer : public QObject {
    Q_OBJECT

public:
    McpServer(const Registry& registry, std::vector<McpTool> tools, QObject* parent = nullptr);

    // No answer means the message was a notification or a response, which HTTP answers with 202.
    std::optional<Json> handle(const Json& message) const;
    // Returns the bound port, which differs from `port` when `port` is 0.
    quint16 listen(quint16 port, bool bindAny);

private:
    [[nodiscard]] std::optional<Json> dispatch(const Json& message) const;
    [[nodiscard]] Json listTools() const;
    [[nodiscard]] Json callTool(const Json& params) const;

    const Registry* registry_;
    std::vector<McpTool> tools_;
    QHttpServer http_;
};

}  // namespace emerick::app
