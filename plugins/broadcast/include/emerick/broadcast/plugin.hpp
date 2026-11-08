#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/signal_tap.hpp"

namespace emerick::broadcast {

class Broadcaster;
struct Profile;
struct Status;

inline constexpr const char* kEnabledSetting = "broadcast.enabled";

// The broadcast profiles in the settings store and the broadcasts that run, for the panel and the live badge.
class Broadcasts : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(bool panelOpen READ panelOpen WRITE setPanelOpen NOTIFY changed)
    // error while a refused broadcast is unseen, live while any is live, retrying while any retries, else empty.
    Q_PROPERTY(QString badge READ badge NOTIFY changed)

public:
    Broadcasts(app::Registry& registry, app::ProjectController& controller);
    ~Broadcasts() override;
    Broadcasts(const Broadcasts&) = delete;
    Broadcasts& operator=(const Broadcasts&) = delete;
    Broadcasts(Broadcasts&&) = delete;
    Broadcasts& operator=(Broadcasts&&) = delete;

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] app::Json listJson() const;
    [[nodiscard]] QVariantList list() const;
    [[nodiscard]] bool panelOpen() const { return panelOpen_; }
    void setPanelOpen(bool open);
    [[nodiscard]] QString badge() const;

    app::Json save(const app::Json& params);
    app::Json remove(const std::string& name);
    app::Json start(const std::string& name);
    app::Json stop(const std::string& name);
    void stopAll();
    // Tests run a fake ffmpeg in place of the one we find.
    void useFfmpeg(std::filesystem::path ffmpeg) { ffmpeg_ = std::move(ffmpeg); }

signals:
    void changed();

private:
    [[nodiscard]] app::Json storedProfiles() const;
    // Throws AppError when no profile has the name.
    [[nodiscard]] Profile profileNamed(const std::string& name) const;
    [[nodiscard]] Status statusOf(const std::string& name) const;
    void launch(const Profile& profile, std::shared_ptr<engine::SignalTap> tap);
    void release(const std::string& name);
    void tick();
    void followSettings();

    app::Registry& registry_;
    app::ProjectController& controller_;
    std::map<std::string, std::unique_ptr<Broadcaster>, std::less<>> running_;
    // The last error of each broadcast the server refused, which the panel shows until it starts or stops again.
    std::map<std::string, std::string, std::less<>> failed_;
    bool errorSeen_ = true;
    std::optional<std::filesystem::path> ffmpeg_;
    bool panelOpen_ = false;
    bool wasEnabled_ = false;
    QTimer refresh_;
};

std::unique_ptr<Broadcasts> registerWith(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);

}  // namespace emerick::broadcast
