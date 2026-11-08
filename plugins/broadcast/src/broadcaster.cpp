// Each broadcast's thread drains its master tap every few milliseconds, writes the PCM to ffmpeg's stdin and the
// encoded stream ffmpeg prints to the server's socket. Nothing on it waits: a full pipe or socket drops audio.
#include "broadcaster.hpp"

#include <QByteArray>
#include <QProcess>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <array>
#include <cstring>
#include <ranges>
#include <utility>

namespace emerick::broadcast {

namespace {

constexpr auto kDrainInterval = std::chrono::milliseconds{10};
constexpr std::size_t kChunkSamples = 8192;
// About a second of stereo float audio at 48 kHz.
constexpr qint64 kMaxBacklogBytes = 384'000;
constexpr int kHttpContinue = 100;
constexpr int kHttpOk = 200;
constexpr int kHttpClientError = 400;
constexpr int kHttpServerError = 500;
constexpr int kKillWaitMs = 2000;
// A connection that stayed up this long starts the backoff over when it drops.
constexpr auto kLongRun = std::chrono::seconds{30};
constexpr auto kAnswerTimeout = std::chrono::seconds{10};
constexpr auto kStallTimeout = std::chrono::seconds{10};

int statusCode(const QByteArray& response) {
    const QList<QByteArray> parts = response.left(response.indexOf('\r')).split(' ');
    return parts.size() > 1 ? parts[1].toInt() : 0;
}

}  // namespace

class Worker : public QObject {
public:
    Worker(Profile profile, std::filesystem::path ffmpeg, double sampleRate, std::shared_ptr<engine::SignalTap> tap,
           std::string title, std::shared_ptr<SharedStatus> status)
        : profile_(std::move(profile)),
          ffmpeg_(std::move(ffmpeg)),
          sampleRate_(sampleRate),
          tap_(std::move(tap)),
          title_(std::move(title)),
          status_(std::move(status)) {}

    void start() {
        drainTimer_ = std::make_unique<QTimer>();
        connect(drainTimer_.get(), &QTimer::timeout, this, [this] { drain(); });
        drainTimer_->start(kDrainInterval);
        retryTimer_ = std::make_unique<QTimer>();
        retryTimer_->setSingleShot(true);
        connect(retryTimer_.get(), &QTimer::timeout, this, [this] { connectToServer(); });
        answerTimer_ = std::make_unique<QTimer>();
        answerTimer_->setSingleShot(true);
        connect(answerTimer_.get(), &QTimer::timeout, this,
                [this] { fail("the server did not answer within 10 s", true); });
        connectToServer();
    }

    void stop() {
        closeConnection();
        drainTimer_.reset();
        retryTimer_.reset();
        answerTimer_.reset();
        metadata_ = nullptr;
        update([](Status& status) {
            status.state = State::idle;
            status.liveSince.reset();
        });
    }

    void setTitle(std::string title) {
        title_ = std::move(title);
        if (encoder_) {
            sendTitle();
        }
    }

private:
    template <class Change>
    void update(Change change) {
        const std::scoped_lock lock{status_->mutex};
        change(status_->status);
    }

    void connectToServer() {
        update([](Status& status) { status.state = State::connecting; });
        response_.clear();
        socket_ = std::make_unique<QSslSocket>();
        connect(socket_.get(), &QSslSocket::readyRead, this, [this] { readResponse(); });
        connect(socket_.get(), &QAbstractSocket::errorOccurred, this,
                [this] { fail("cannot reach the server: " + socket_->errorString().toStdString(), true); });
        connect(socket_.get(), &QAbstractSocket::disconnected, this,
                [this] { fail("the server closed the connection", true); });
        open(*socket_);
        socket_->write(QByteArray::fromStdString(sourceRequest(profile_)));
        answerTimer_->start(kAnswerTimeout);
    }

    void open(QSslSocket& socket) const {
        const QString host = QString::fromStdString(profile_.host);
        const auto port = static_cast<quint16>(profile_.port);
        if (profile_.tls) {
            socket.connectToHostEncrypted(host, port);
        } else {
            socket.connectToHost(host, port);
        }
    }

    void readResponse() {
        if (encoder_) {
            socket_->readAll();
            return;
        }
        response_ += socket_->readAll();
        if (!response_.contains("\r\n\r\n")) {
            return;
        }
        const int code = statusCode(response_);
        if (code != kHttpContinue && code != kHttpOk) {
            const bool refused = code >= kHttpClientError && code < kHttpServerError;
            fail("the server answered " + response_.left(response_.indexOf('\r')).toStdString(), !refused);
            return;
        }
        startEncoder();
    }

    void startEncoder() {
        answerTimer_->stop();
        encoder_ = std::make_unique<QProcess>();
        connect(encoder_.get(), &QProcess::readyReadStandardOutput, this, [this] {
            const QByteArray encoded = encoder_->readAllStandardOutput();
            socket_->write(encoded);
            update([&](Status& status) { status.bytesSent += encoded.size(); });
        });
        connect(encoder_.get(), &QProcess::readyReadStandardError, this, [this] {
            if (const QByteArray text = encoder_->readAllStandardError().trimmed(); !text.isEmpty()) {
                encoderError_ = text.split('\n').constLast().toStdString();
            }
        });
        connect(encoder_.get(), &QProcess::finished, this, [this](int code) {
            fail(encoderError_.empty() ? "ffmpeg stopped with code " + std::to_string(code)
                                       : "ffmpeg stopped: " + encoderError_,
                 true);
        });
        connect(encoder_.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                fail("cannot start " + ffmpeg_.string(), true);
            }
        });
        const auto arguments = encoderArguments(profile_, sampleRate_, title_) |
                               std::views::transform(&QString::fromStdString) | std::ranges::to<QStringList>();
        encoder_->start(QString::fromStdString(ffmpeg_.string()), arguments);
        update([](Status& status) {
            status.state = State::live;
            status.liveSince = std::chrono::steady_clock::now();
            status.lastError.clear();
        });
        sendTitle();
    }

    // Icecast takes a title update for MP3 only; an Ogg stream carries its title from the start.
    void sendTitle() {
        if (profile_.format != Format::mp3) {
            return;
        }
        if (metadata_) {
            metadata_.release()->deleteLater();
        }
        metadata_ = std::make_unique<QSslSocket>();
        connect(metadata_.get(), &QSslSocket::readyRead, this, [this] { metadata_->disconnectFromHost(); });
        open(*metadata_);
        metadata_->write(QByteArray::fromStdString(metadataRequest(profile_, title_)));
    }

    // A socket that stays backed up this long has stalled, so we reconnect.
    void checkStall() {
        if (!encoder_ || socket_->bytesToWrite() <= kMaxBacklogBytes) {
            backedUpSince_.reset();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!backedUpSince_) {
            backedUpSince_ = now;
        } else if (now - *backedUpSince_ > kStallTimeout) {
            fail("the connection stalled", true);
        }
    }

    void drain() {
        checkStall();
        std::array<float, kChunkSamples> chunk{};
        while (const std::size_t count = tap_->ring.read(chunk)) {
            if (!encoder_ || encoder_->state() != QProcess::Running) {
                continue;
            }
            if (encoder_->bytesToWrite() > kMaxBacklogBytes || socket_->bytesToWrite() > kMaxBacklogBytes) {
                update([&](Status& status) { status.droppedFrames += static_cast<std::int64_t>(count / 2); });
                continue;
            }
            QByteArray bytes(static_cast<qsizetype>(count * sizeof(float)), Qt::Uninitialized);
            std::memcpy(bytes.data(), chunk.data(), count * sizeof(float));
            encoder_->write(bytes);
        }
    }

    // A signal of the socket or the process can be what brought us here, so we delete them once it returns.
    void closeConnection() {
        backedUpSince_.reset();
        if (answerTimer_) {
            answerTimer_->stop();
        }
        if (encoder_) {
            encoder_->disconnect(this);
            encoder_->kill();
            encoder_->waitForFinished(kKillWaitMs);
            encoder_.release()->deleteLater();
        }
        if (socket_) {
            socket_->disconnect(this);
            socket_->abort();
            socket_.release()->deleteLater();
        }
        encoderError_.clear();
    }

    void fail(const std::string& message, bool retry) {
        if (!socket_) {
            return;
        }
        closeConnection();
        update([&](Status& status) {
            const bool ranLong = status.liveSince && std::chrono::steady_clock::now() - *status.liveSince > kLongRun;
            status.failures = ranLong ? 1 : status.failures + 1;
            status.liveSince.reset();
            status.lastError = message;
            status.state = retry ? State::retrying : State::error;
            status.retryIn = retry ? retryDelay(status.failures) : std::chrono::seconds{0};
            if (retry) {
                retryTimer_->start(status.retryIn);
            }
        });
    }

    Profile profile_;
    std::filesystem::path ffmpeg_;
    double sampleRate_;
    std::shared_ptr<engine::SignalTap> tap_;
    std::string title_;
    std::shared_ptr<SharedStatus> status_;
    std::unique_ptr<QTimer> drainTimer_;
    std::unique_ptr<QTimer> retryTimer_;
    std::unique_ptr<QTimer> answerTimer_;
    std::optional<std::chrono::steady_clock::time_point> backedUpSince_;
    std::unique_ptr<QSslSocket> socket_;
    std::unique_ptr<QSslSocket> metadata_;
    std::unique_ptr<QProcess> encoder_;
    QByteArray response_;
    std::string encoderError_;
};

Broadcaster::Broadcaster(Profile profile, std::filesystem::path ffmpeg, double sampleRate,
                         std::shared_ptr<engine::SignalTap> tap, std::string title)
    : sampleRate_(sampleRate),
      tap_(std::move(tap)),
      status_(std::make_shared<SharedStatus>()),
      worker_(std::make_unique<Worker>(std::move(profile), std::move(ffmpeg), sampleRate, tap_, std::move(title),
                                       status_)) {
    worker_->moveToThread(&thread_);
    thread_.start();
    QMetaObject::invokeMethod(worker_.get(), [worker = worker_.get()] { worker->start(); });
}

Broadcaster::~Broadcaster() {
    QMetaObject::invokeMethod(
        worker_.get(), [worker = worker_.get()] { worker->stop(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void Broadcaster::setTitle(const std::string& title) {
    // NOLINTNEXTLINE(bugprone-exception-escape): running out of memory on the broadcast thread ends the process.
    QMetaObject::invokeMethod(worker_.get(), [worker = worker_.get(), title] { worker->setTitle(title); });
}

}  // namespace emerick::broadcast
