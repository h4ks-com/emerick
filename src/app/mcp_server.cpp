#include "emerick/app/mcp_server.hpp"

#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QTcpServer>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "emerick/app/core_actions.hpp"

namespace emerick::app {

namespace {

constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
constexpr int kParseError = -32700;

struct RpcError {
    int code;
    std::string message;
};

using Method = Json (*)(const McpServer& server, const Json& params);

Json rpcError(const Json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

// Text we did not write, such as a UI label or a Lua string, can hold invalid UTF-8, which a strict dump throws on.
std::string dumped(const Json& value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }

bool isLoopbackOrigin(const QByteArray& origin) {
    const QString host = QUrl(QString::fromUtf8(origin)).host();
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

const Json* field(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

template <class Check>
const Json* typedField(const Json& object, const char* key, Check check, int code, const char* expected) {
    const Json* value = field(object, key);
    if (value != nullptr && !check(*value)) {
        throw RpcError{code, std::string{key} + " must be " + expected};
    }
    return value;
}

bool isString(const Json& value) { return value.is_string(); }
bool isObject(const Json& value) { return value.is_object(); }
bool isObjectOrNull(const Json& value) { return value.is_object() || value.is_null(); }
bool isId(const Json& value) { return value.is_string() || value.is_number() || value.is_null(); }

Json initializeResult(const Json& params) {
    const Json* requested = typedField(params, "protocolVersion", isString, kInvalidParams, "a string");
    const auto* supported = requested == nullptr
                                ? kMcpProtocolVersions.end()
                                : std::ranges::find(kMcpProtocolVersions, requested->get<std::string>());
    return {{"protocolVersion", supported != kMcpProtocolVersions.end() ? *supported : kMcpProtocolVersions.front()},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "emerick"}, {"version", appVersion()}}},
            {"instructions",
             "Find actions and settings with ui.find or actions.list, run them by id with actions.run, and read the "
             "result state that every tool returns."}};
}

}  // namespace

McpServer::McpServer(const Registry& registry, std::vector<McpTool> tools, QObject* parent)
    : QObject(parent), registry_(&registry), tools_(std::move(tools)) {}

std::optional<Json> McpServer::handle(const Json& message) const {
    const Json* id = message.is_object() ? field(message, "id") : nullptr;
    const Json replyId = id != nullptr && isId(*id) ? *id : Json(nullptr);
    try {
        return dispatch(message);
    } catch (const RpcError& error) {
        return rpcError(replyId, error.code, error.message);
    } catch (const Json::exception& error) {
        return rpcError(replyId, kInternalError, error.what());
    } catch (const AppError& error) {
        return rpcError(replyId, kInternalError, error.what());
    }
}

std::optional<Json> McpServer::dispatch(const Json& message) const {
    static constexpr std::array<std::pair<std::string_view, Method>, 4> kMethods{{
        {"initialize", [](const McpServer&, const Json& params) { return initializeResult(params); }},
        {"ping", [](const McpServer&, const Json&) { return Json::object(); }},
        {"tools/list", [](const McpServer& server, const Json&) { return server.listTools(); }},
        {"tools/call", [](const McpServer& server, const Json& params) { return server.callTool(params); }},
    }};
    if (!message.is_object()) {
        throw RpcError{kInvalidRequest, "expected a JSON-RPC 2.0 object"};
    }
    const Json* version = field(message, "jsonrpc");
    if (version == nullptr || *version != "2.0") {
        throw RpcError{kInvalidRequest, "jsonrpc must be \"2.0\""};
    }
    const Json* id = typedField(message, "id", isId, kInvalidRequest, "a string, a number or null");
    const Json* method = typedField(message, "method", isString, kInvalidRequest, "a string");
    if (method == nullptr || id == nullptr) {
        return std::nullopt;
    }
    const Json* params = typedField(message, "params", isObject, kInvalidParams, "an object");
    const auto name = method->get<std::string>();
    const auto* found = std::ranges::find(kMethods, name, &std::pair<std::string_view, Method>::first);
    if (found == kMethods.end()) {
        throw RpcError{kMethodNotFound, "no method " + name};
    }
    return Json{{"jsonrpc", "2.0"},
                {"id", *id},
                {"result", found->second(*this, params != nullptr ? *params : Json::object())}};
}

Json McpServer::listTools() const {
    Json tools = Json::array();
    for (const McpTool& tool : tools_) {
        tools.push_back({{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
    }
    return {{"tools", tools}};
}

Json McpServer::callTool(const Json& params) const {
    const Json* name = typedField(params, "name", isString, kInvalidParams, "a string");
    const Json* arguments = typedField(params, "arguments", isObjectOrNull, kInvalidParams, "an object");
    if (name == nullptr) {
        throw RpcError{kInvalidParams, "name must be a string"};
    }
    const auto tool = std::ranges::find(tools_, name->get<std::string>(), &McpTool::name);
    if (tool == tools_.end()) {
        throw RpcError{kInvalidParams, "no tool " + name->get<std::string>()};
    }
    Json result;
    try {
        result = tool->call(arguments != nullptr && arguments->is_object() ? *arguments : Json::object());
    } catch (const AppError& error) {
        result = {{"error", error.what()}};
    } catch (const Json::exception& error) {
        result = {{"error", std::string{"invalid arguments: "} + error.what()}};
    }
    if (!result.is_object()) {
        result = Json{{"value", std::move(result)}};
    }
    const bool isError = result.contains("error");
    result["state"] = registry_->allState();
    return {{"content", {{{"type", "text"}, {"text", dumped(result)}}}},
            {"structuredContent", result},
            {"isError", isError}};
}

quint16 McpServer::listen(quint16 port, bool bindAny) {
    http_.route(
        "/mcp", QHttpServerRequest::Method::Post, this,
        [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
            using Status = QHttpServerResponder::StatusCode;
            const QByteArray origin = request.headers().value(QHttpHeaders::WellKnownHeader::Origin).toByteArray();
            if (!origin.isEmpty() && !isLoopbackOrigin(origin)) {
                responder.write(Status::Forbidden);
                return;
            }
            const QByteArray version = request.headers().value("MCP-Protocol-Version").toByteArray();
            if (!version.isEmpty() &&
                std::ranges::find(kMcpProtocolVersions, version.toStdString()) == kMcpProtocolVersions.end()) {
                responder.write(Status::BadRequest);
                return;
            }
            const Json message = Json::parse(request.body().toStdString(), nullptr, false);
            if (message.is_discarded()) {
                responder.write(QByteArray::fromStdString(dumped(rpcError(nullptr, kParseError, "invalid JSON"))),
                                "application/json", Status::BadRequest);
                return;
            }
            if (const auto answer = handle(message)) {
                responder.write(QByteArray::fromStdString(dumped(*answer)), "application/json");
            } else {
                responder.write(Status::Accepted);
            }
        });
    http_.route("/mcp", QHttpServerRequest::Method::Get | QHttpServerRequest::Method::Delete, this,
                [] { return QHttpServerResponse(QHttpServerResponder::StatusCode::MethodNotAllowed); });

    auto tcp = std::make_unique<QTcpServer>();
    if (!tcp->listen(bindAny ? QHostAddress::Any : QHostAddress::LocalHost, port) || !http_.bind(tcp.get())) {
        throw AppError("cannot listen for MCP on port " + std::to_string(port));
    }
    return tcp.release()->serverPort();
}

}  // namespace emerick::app
