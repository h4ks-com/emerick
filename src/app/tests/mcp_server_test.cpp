#include "emerick/app/mcp_server.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>

#include "emerick/app/core_actions.hpp"
#include "qt_app.hpp"

using namespace emerick::app;

namespace {

struct Fixture {
    bool application = (ensureApplication(), true);
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    UiApi ui{registry};
    LuaHost lua{registry, ui};
    McpServer server{registry, mcpTools(registry, ui, &lua)};

    Fixture() { registerCoreActions(registry, nav, {}); }

    std::optional<Json> call(const Json& message) { return server.handle(message); }

    Json tool(const std::string& name, const Json& arguments) {
        return call({{"jsonrpc", "2.0"},
                     {"id", 7},
                     {"method", "tools/call"},
                     {"params", {{"name", name}, {"arguments", arguments}}}})
            ->at("result");
    }

    int errorCode(const Json& message) { return call(message)->at("error").at("code").get<int>(); }
};

}  // namespace

TEST_CASE("initialize negotiates the protocol version") {
    Fixture fixture;
    const auto reply = fixture.call(
        {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-06-18"}}}});
    REQUIRE(reply);
    CHECK(reply->at("id") == 1);
    CHECK(reply->at("result").at("protocolVersion") == "2025-06-18");
    CHECK(reply->at("result").at("serverInfo").at("name") == "emerick");

    const auto future = fixture.call(
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "initialize"}, {"params", {{"protocolVersion", "2099-01-01"}}}});
    CHECK(future->at("result").at("protocolVersion") == kMcpProtocolVersions.front());
}

TEST_CASE("notifications get no reply and unknown methods an error") {
    Fixture fixture;
    CHECK_FALSE(fixture.call({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    CHECK(fixture.errorCode({{"jsonrpc", "2.0"}, {"id", 3}, {"method", "resources/list"}}) == -32601);
    CHECK(fixture.errorCode(Json::array()) == -32600);
}

TEST_CASE("a malformed message gets a JSON-RPC error and never throws") {
    Fixture fixture;
    CHECK(fixture.errorCode({{"jsonrpc", 2}}) == -32600);
    CHECK(fixture.errorCode({{"jsonrpc", "2.0"}, {"id", 1}, {"method", 5}}) == -32600);
    CHECK(fixture.errorCode({{"jsonrpc", "2.0"}, {"id", Json::object()}, {"method", "ping"}}) == -32600);
    CHECK(fixture.errorCode({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}, {"params", 3}}) == -32602);
    CHECK(fixture.errorCode(
              {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", 1}}}}) ==
          -32602);
    CHECK(fixture.errorCode({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", 1}}}}) ==
          -32602);
    CHECK(fixture.errorCode({{"jsonrpc", "2.0"},
                             {"id", 1},
                             {"method", "tools/call"},
                             {"params", {{"name", "ui.find"}, {"arguments", "x"}}}}) == -32602);
    CHECK(fixture.tool("ui.find", {{"query", 5}}).at("isError") == true);
    CHECK(fixture.tool("settings.get", {{"id", Json::array()}}).at("isError") == true);
}

TEST_CASE("text that is not UTF-8 comes back replaced") {
    Fixture fixture;
    const Json result = fixture.tool("lua.eval", {{"code", "return '\\xff'"}});
    CHECK(result.at("isError") == false);
    CHECK_FALSE(result.at("content").at(0).at("text").get<std::string>().empty());
}

TEST_CASE("tools/list names every tool") {
    Fixture fixture;
    const auto reply = fixture.call({{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/list"}});
    std::vector<std::string> names;
    for (const Json& tool : reply->at("result").at("tools")) {
        names.push_back(tool.at("name"));
        CHECK(tool.at("inputSchema").at("type") == "object");
    }
    CHECK(names == std::vector<std::string>{"ui.tree", "ui.find", "ui.reveal", "ui.invoke", "ui.set", "actions.list",
                                            "actions.run", "actions.batch", "settings.get", "settings.set", "state.get",
                                            "lua.eval", "screenshot"});
}

TEST_CASE("state.get lists the active limits") {
    Fixture fixture;
    const Json open = fixture.tool("state.get", {{"name", "limits"}}).at("structuredContent").at("limits");
    CHECK(open == Json{{"luaTimeoutMs", nullptr},
                       {"luaMemoryMb", 256},
                       {"maxRenderSeconds", nullptr},
                       {"bundledPluginsOnly", false},
                       {"libraryInstall", true},
                       {"agentReadRoot", nullptr},
                       {"agentWriteRoot", nullptr}});

    fixture.registry.setLimits({.luaTimeout = std::chrono::milliseconds{2000},
                                .luaMemoryBytes = std::size_t{64} << 20,
                                .renderSeconds = 600,
                                .bundledPluginsOnly = true,
                                .libraryInstall = false});
    fixture.registry.setAgentRoots({.read = std::nullopt, .write = fixture.dir.path().toStdString()});
    const Json limited = fixture.tool("state.get", {{"name", "limits"}}).at("structuredContent").at("limits");
    CHECK(limited.at("luaTimeoutMs") == 2000);
    CHECK(limited.at("luaMemoryMb") == 64);
    CHECK(limited.at("maxRenderSeconds") == 600.0);
    CHECK(limited.at("bundledPluginsOnly") == true);
    CHECK(limited.at("libraryInstall") == false);
    CHECK(limited.at("agentReadRoot").is_null());
    CHECK(limited.at("agentWriteRoot") == std::filesystem::canonical(fixture.dir.path().toStdString()).string());
}

TEST_CASE("actions.batch runs calls in order, stops at the first error unless asked to keep going") {
    Fixture fixture;
    const Json calls{{{"id", "settings.show.audio"}},
                     {{"id", "nothing.here"}},
                     {{"id", "settings.show.general"}, {"params", Json::object()}}};
    const Json stopped = fixture.tool("actions.batch", {{"calls", calls}}).at("structuredContent").at("results");
    REQUIRE(stopped.size() == 2);
    CHECK(stopped.at(0).contains("result"));
    CHECK(stopped.at(1).at("error") == "no action nothing.here");

    const Json all = fixture.tool("actions.batch", {{"calls", calls}, {"keepGoing", true}});
    CHECK(all.at("structuredContent").at("results").size() == 3);
    CHECK(all.at("structuredContent").at("state").at("nav").at("settingsTab") == "general");

    fixture.tool("actions.run", {{"id", "settings.show.audio"}});
    const Json tooMany = fixture.tool(
        "actions.batch", {{"calls", Json(std::vector<Json>(kMaxBatchCalls + 1, {{"id", "settings.show.general"}}))}});
    CHECK(tooMany.at("isError") == true);
    CHECK(tooMany.at("structuredContent").at("state").at("nav").at("settingsTab") == "audio");
    CHECK(fixture.tool("actions.batch", {{"calls", {{{"params", Json::object()}}}}}).at("isError") == true);

    const Json lua =
        fixture.tool("lua.eval", {{"code", "return emerick.batch({{id = 'settings.show.audio'}, {id = 'x'}}, true)"}});
    CHECK(lua.at("structuredContent").at("result").at("results").at(1).at("error") == "no action x");
}

TEST_CASE("tools return the resulting state") {
    Fixture fixture;
    const Json run = fixture.tool("actions.run", {{"id", "settings.show.audio"}});
    CHECK(run.at("isError") == false);
    CHECK(run.at("structuredContent").at("state").at("nav").at("settingsTab") == "audio");
    CHECK(Json::parse(run.at("content").at(0).at("text").get<std::string>()) == run.at("structuredContent"));

    const Json set = fixture.tool("settings.set", {{"id", "audio.bufferSize"}, {"value", 512}});
    CHECK(set.at("structuredContent").at("setting").at("value") == 512);
    CHECK(fixture.tool("settings.set", {{"id", "audio.bufferSize"}, {"value", 3}}).at("isError") == true);
    CHECK(fixture.tool("settings.set", {{"id", "mcp.port"}, {"value", 70000}}).at("isError") == true);
    CHECK(fixture.tool("actions.run", {}).at("isError") == true);

    const Json listed = fixture.tool("actions.list", {{"query", "quit"}});
    CHECK(listed.at("structuredContent").at("actions").at(0).at("id") == "app.quit");
    const Json lua = fixture.tool("lua.eval", {{"code", "return emerick.setting('audio.bufferSize')"}});
    CHECK(lua.at("structuredContent").at("result") == 512);
    CHECK(fixture.tool("lua.eval", {{"code", "error('boom')"}}).at("isError") == true);
}

TEST_CASE("unknown tools are a JSON-RPC error") {
    Fixture fixture;
    CHECK(fixture.errorCode(
              {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "tools/call"}, {"params", {{"name", "ui.click"}}}}) == -32602);
}

TEST_CASE("one HTTP round trip on a random port") {
    Fixture fixture;
    const quint16 port = fixture.server.listen(0, false);
    REQUIRE(port != 0);
    QNetworkAccessManager network;
    const auto post = [&](const QByteArray& body, const QByteArray& origin) {
        QNetworkRequest request{QUrl{QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)}};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", "application/json, text/event-stream");
        if (!origin.isEmpty()) {
            request.setRawHeader("Origin", origin);
        }
        QNetworkReply* reply = network.post(request, body);
        REQUIRE(processUntil([reply] { return reply->isFinished(); }));
        reply->deleteLater();
        return std::pair{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll()};
    };

    const auto [status, body] = post(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", {});
    CHECK(status == 200);
    CHECK(Json::parse(body.toStdString()).at("result").at("protocolVersion") == kMcpProtocolVersions.front());
    CHECK(post(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", {}).first == 202);
    CHECK(post(R"({"jsonrpc":"2.0","id":2,"method":"ping"})", "https://evil.example").first == 403);
    CHECK(post(R"({"jsonrpc":"2.0","id":2,"method":"ping"})", "http://localhost:8888").first == 200);
    CHECK(post("not json", {}).first == 400);
    const auto [malformedStatus, malformed] = post(R"({"jsonrpc":2})", {});
    CHECK(malformedStatus == 200);
    CHECK(Json::parse(malformed.toStdString()).at("error").at("code") == -32600);
}
