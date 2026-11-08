#include <QByteArray>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "emerick/app/console.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/ui_api.hpp"
#include "emerick/broadcast/plugin.hpp"
#include "profile.hpp"
#include "project_rig.hpp"

using namespace emerick;
using app::Json;

namespace {

const std::string kPassword = "eyJ" + std::string(320, 'k');
const std::string kMount = "/emerick-test-mount";

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return true;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Answers each source request the way Icecast does and keeps what the source sends.
struct FakeIcecast {
    QTcpServer server;
    std::string answer = "HTTP/1.1 100 Continue\r\n\r\n";
    std::vector<std::string> requests;
    std::string body;

    FakeIcecast() {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto received = std::make_shared<std::string>();
            QObject::connect(socket, &QTcpSocket::readyRead, [this, socket, received] {
                const QByteArray bytes = socket->readAll();
                if (received->contains("\r\n\r\n")) {
                    body += bytes.toStdString();
                    return;
                }
                *received += bytes.toStdString();
                if (const auto end = received->find("\r\n\r\n"); end != std::string::npos) {
                    requests.push_back(received->substr(0, end));
                    body += received->substr(end + 4);
                    socket->write(QByteArray::fromStdString(answer));
                }
            });
        });
    }
    [[nodiscard]] int port() const { return server.serverPort(); }
};

struct BroadcastRig {
    app::test::ProjectRig rig;
    FakeIcecast icecast;
    QTemporaryDir fake;
    std::unique_ptr<broadcast::Broadcasts> broadcasts = broadcast::registerWith(rig.registry, *rig.controller, rig.nav);

    explicit BroadcastRig(const char* mode = "record") {
        qputenv("FAKE_FFMPEG_DIR", fake.path().toUtf8());
        qputenv("FAKE_FFMPEG_MODE", mode);
        broadcasts->useFfmpeg(FAKE_FFMPEG);
    }

    void enableAndSave() {
        rig.registry.setSetting(broadcast::kEnabledSetting, true);
        rig.run("broadcast.save", {{"name", "Home"},
                                   {"host", "127.0.0.1"},
                                   {"port", icecast.port()},
                                   {"mount", kMount},
                                   {"password", kPassword},
                                   {"streamName", "Late session"},
                                   {"genre", "Jazz"}});
    }

    [[nodiscard]] Json status() const { return rig.run("broadcast.list").at("broadcasts").at(0); }
    [[nodiscard]] std::string stdinBytes() const { return readFile(fake.filePath("stdin.raw").toStdString()); }
    [[nodiscard]] std::string arguments() const { return readFile(fake.filePath("args").toStdString()); }
};

bool hasSound(const std::string& bytes) {
    for (std::size_t offset = 0; offset + sizeof(float) <= bytes.size(); offset += sizeof(float)) {
        float sample = 0.0F;
        std::memcpy(&sample, bytes.data() + offset, sizeof(float));
        if (sample != 0.0F) {
            return true;
        }
    }
    return false;
}

bool listsAction(const QVariantList& items, const QString& id) {
    return std::ranges::any_of(items, [&](const QVariant& item) { return item.toMap().value("id") == id; });
}

}  // namespace

TEST_CASE("broadcasting starts off, hidden from menus and the palette, and its actions refuse until it is on") {
    BroadcastRig broadcast;
    auto& registry = broadcast.rig.registry;
    CHECK_FALSE(listsAction(registry.menus().value("Transport").toList(), "broadcast.show"));
    CHECK_FALSE(listsAction(registry.palette("broadcast"), "broadcast.show"));
    CHECK(broadcast.rig.run("broadcast.list").at("enabled") == false);
    try {
        broadcast.rig.run("broadcast.start", {{"name", "Home"}});
        FAIL("broadcast.start ran while broadcasting was off");
    } catch (const app::AppError& error) {
        CHECK(std::string{error.what()} == "turn on broadcasting in Settings / Plugins first");
    }

    broadcast.enableAndSave();
    CHECK(listsAction(registry.menus().value("Transport").toList(), "broadcast.show"));
    CHECK(listsAction(registry.palette("show broadcasts"), "broadcast.show"));
    CHECK(broadcast.rig.run("broadcast.list").at("broadcasts").size() == 1);
    CHECK_THROWS_AS(broadcast.rig.run("broadcast.stop", {{"name", "Nowhere"}}), app::AppError);

    registry.setSetting(broadcast::kEnabledSetting, false);
    CHECK(broadcast.rig.run("broadcast.list") == Json{{"enabled", false}, {"broadcasts", Json::array()}});
}

TEST_CASE("a profile keeps its password in the settings store and masks it everywhere else") {
    BroadcastRig broadcast;
    broadcast.enableAndSave();
    auto& registry = broadcast.rig.registry;
    CHECK(registry.privateValue("broadcast.profiles").dump().contains(kPassword));
    CHECK(broadcast.status().at("password") == "********");
    for (const Json& shown : {registry.allState(), registry.describeSettings(), broadcast.rig.run("broadcast.list"),
                              registry.describeActions("broadcast")}) {
        CHECK_FALSE(shown.dump().contains(kPassword));
    }

    broadcast.rig.run("broadcast.save", {{"name", "Home"}, {"bitrate", 192}});
    CHECK(registry.privateValue("broadcast.profiles").dump().contains(kPassword));
    CHECK(broadcast.status().at("bitrate") == 192);
    const auto permissions = QFileInfo{broadcast.rig.dir.filePath("settings.ini")}.permissions();
    CHECK((permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther)) == QFileDevice::Permissions{});
}

TEST_CASE("the action log and errors show a password masked") {
    BroadcastRig broadcast;
    const app::UiApi ui(broadcast.rig.registry);
    app::LuaHost lua(broadcast.rig.registry, ui);
    const app::Console console(broadcast.rig.registry, lua);
    broadcast.enableAndSave();
    CHECK(console.recent(100).dump().contains("broadcast.save"));
    CHECK_FALSE(console.recent(100).dump().contains(kPassword));
    try {
        broadcast.rig.run("broadcast.save", {{"name", "Home"}, {"password", 31415926}});
        FAIL("broadcast.save took a number as its password");
    } catch (const app::AppError& error) {
        CHECK(std::string{error.what()} == "param password must be a string");
    }
}

TEST_CASE("saving and reopening a project writes none of a broadcast profile into the project's files") {
    BroadcastRig broadcast;
    broadcast.enableAndSave();
    broadcast.rig.run("track.addAudio", {{"name", "Vox"}});
    const auto song = broadcast.rig.path("Songs/Show.erk");
    broadcast.rig.run("project.saveAs", {{"path", song.string()}});
    broadcast.rig.run("project.new");
    broadcast.rig.run("project.open", {{"path", song.string()}});
    broadcast.rig.run("project.save");

    for (const auto& entry : std::filesystem::recursive_directory_iterator(song)) {
        if (entry.is_regular_file()) {
            const std::string text = readFile(entry.path());
            CHECK_FALSE(text.contains(kPassword));
            CHECK_FALSE(text.contains(kMount));
            CHECK_FALSE(text.contains("Late session"));
        }
    }
    CHECK_FALSE(broadcast.rig.run("project.summary").dump().contains(kMount));
}

TEST_CASE("a broadcast feeds the master to ffmpeg while stopped and playing, and holds the stream until it stops") {
    BroadcastRig broadcast;
    broadcast.enableAndSave();
    auto& engine = broadcast.rig.controller->engine();
    broadcast.rig.run("broadcast.start", {{"name", "Home"}});
    REQUIRE(waitFor([&] { return broadcast.status().at("state") == "live"; }));
    CHECK(engine.streamOpen());

    REQUIRE(waitFor([&] { return broadcast.arguments().contains("pipe:1"); }));
    const std::string arguments = broadcast.arguments();
    CHECK(arguments.contains("f32le\n-ar\n48000\n-ac\n2\n-i\npipe:0\n-c:a\nlibopus"));
    CHECK_FALSE(arguments.contains(kPassword));
    REQUIRE(broadcast.icecast.requests.size() == 1);
    const std::string& request = broadcast.icecast.requests.front();
    CHECK(request.starts_with("PUT " + kMount + " HTTP/1.1"));
    CHECK(request.contains("Authorization: Basic " +
                           QByteArray::fromStdString("source:" + kPassword).toBase64().toStdString()));
    CHECK(request.contains("Content-Type: audio/ogg"));
    CHECK(request.contains("Ice-Name: Late session"));
    CHECK(request.contains("Ice-Genre: Jazz"));

    CHECK(waitFor([&] { return broadcast.stdinBytes().size() > 48000; }));
    CHECK_FALSE(hasSound(broadcast.stdinBytes()));

    const std::string vox = broadcast.rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    app::test::writeConstant(broadcast.rig.path("ones.wav"), 48000, 0.5F);
    broadcast.rig.run("clip.import", {{"track", vox}, {"path", broadcast.rig.path("ones.wav").string()}});
    broadcast.rig.run("transport.play");
    CHECK(waitFor([&] { return hasSound(broadcast.stdinBytes()); }));
    CHECK(waitFor([&] { return broadcast.status().at("bytesSent").get<int>() > 0; }));
    CHECK(waitFor([&] { return broadcast.icecast.body.size() > 48000; }));
    broadcast.rig.run("transport.stop");

    broadcast.rig.run("broadcast.stop", {{"name", "Home"}});
    CHECK(broadcast.status().at("state") == "idle");
    CHECK(waitFor([&] { return !engine.streamOpen(); }));
    CHECK_FALSE(broadcast.rig.registry.allState().dump().contains(kPassword));
}

TEST_CASE("a dying ffmpeg retries after 1, 2, 4 and up to 30 seconds, and stop ends the retries") {
    CHECK(broadcast::retryDelay(1).count() == 1);
    CHECK(broadcast::retryDelay(2).count() == 2);
    CHECK(broadcast::retryDelay(4).count() == 8);
    CHECK(broadcast::retryDelay(9).count() == 30);

    BroadcastRig broadcast("die");
    broadcast.enableAndSave();
    broadcast.rig.run("broadcast.start", {{"name", "Home"}});
    REQUIRE(waitFor([&] { return broadcast.status().at("state") == "retrying"; }));
    CHECK(broadcast.status().at("retryInSeconds") == 1);
    CHECK(broadcast.status().at("lastError") == "ffmpeg stopped: fake encoder failed");
    REQUIRE(waitFor(
        [&] { return broadcast.icecast.requests.size() == 2 && broadcast.status().at("retryInSeconds") == 2; }));

    broadcast.rig.run("broadcast.stop", {{"name", "Home"}});
    waitFor([] { return false; }, std::chrono::milliseconds{2500});
    CHECK(broadcast.icecast.requests.size() == 2);
    CHECK(broadcast.status().at("state") == "idle");
}

TEST_CASE("an ffmpeg that stops reading drops audio and counts it, and never blocks") {
    BroadcastRig broadcast("stall");
    broadcast.enableAndSave();
    broadcast.rig.run("broadcast.start", {{"name", "Home"}});
    // The drop needs about a second of audio in real time beyond the pipe's own buffer, which a slow runner takes
    // longer to produce.
    CHECK(waitFor([&] { return broadcast.status().at("droppedFrames").get<int>() > 0; }, std::chrono::seconds{20}));
    const auto before = std::chrono::steady_clock::now();
    broadcast.rig.run("broadcast.stop", {{"name", "Home"}});
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds{1});
}

TEST_CASE("a server that refuses the source stops with its answer and never shows the password") {
    BroadcastRig broadcast;
    broadcast.icecast.answer = "HTTP/1.1 401 Authentication Required\r\n\r\n";
    broadcast.enableAndSave();
    broadcast.rig.run("broadcast.start", {{"name", "Home"}});
    REQUIRE(waitFor([&] { return broadcast.status().at("state") == "error"; }));
    CHECK(broadcast.status().at("lastError") == "the server answered HTTP/1.1 401 Authentication Required");
    CHECK(broadcast.arguments().empty());
    CHECK(waitFor([&] { return !broadcast.rig.controller->engine().streamOpen(); }));
    CHECK(broadcast.broadcasts->badge() == "error");
    broadcast.broadcasts->setPanelOpen(true);
    CHECK(broadcast.broadcasts->badge().isEmpty());
    CHECK(broadcast.status().at("state") == "error");
    broadcast.rig.run("broadcast.stop", {{"name", "Home"}});
    CHECK(broadcast.status().at("state") == "idle");
}

TEST_CASE("turning broadcasting off stops every broadcast") {
    BroadcastRig broadcast;
    broadcast.enableAndSave();
    broadcast.rig.run("broadcast.start", {{"name", "Home"}});
    REQUIRE(waitFor([&] { return broadcast.status().at("state") == "live"; }));
    broadcast.rig.registry.setSetting(broadcast::kEnabledSetting, false);
    CHECK(broadcast.broadcasts->badge().isEmpty());
    CHECK(waitFor([&] { return !broadcast.rig.controller->engine().streamOpen(); }));
}
