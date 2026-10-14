#include "include/api/remote/Server.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QMutexLocker>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>

#include "include/global/Utils.hpp"

namespace RemoteApi {
    class Server::Worker : public QObject {
    public:
        explicit Worker(Server *server);

        ~Worker() override;

        void apply(const Config &config);

    private:
        struct Mailbox {
            QMutex mutex;
            Worker *worker = nullptr;
        };

        // Shared by every copy of one Responder; a Responder dropped unanswered answers 500.
        struct Exchange {
            std::shared_ptr<Mailbox> mailbox;
            quint64 connectionId = 0;
            quint64 number = 0;
            std::atomic_bool answered{false};

            ~Exchange();

            void answer(const Response &response);
        };

        enum class Phase { Hello, Ready, Pending };

        struct Connection : QObject {
            using QObject::QObject;

            quint64 id = 0;
            QHostAddress address;
            QString peer;
            QTcpSocket *socket = nullptr;
            QTimer *timer = nullptr;
            Phase phase = Phase::Hello;
            bool closed = false;
            QByteArray serverRandom;
            SessionKeys keys;
            quint64 received = 0;
            quint64 sent = 0;
            quint64 exchanges = 0;
            QByteArray header;
            quint32 length = 0;
            QByteArray body;
        };

        struct FailureRecord {
            QList<qint64> recent;
            qint64 refusedUntil = 0;
        };

        static constexpr int kMaxConnections = 32;
        static constexpr int kMaxConnectionsPerPeer = 8;
        static constexpr int kHelloTimeoutMs = 10 * 1000;
        static constexpr int kIdleTimeoutMs = 60 * 1000;
        static constexpr int kFailureLimit = 5;
        static constexpr qint64 kFailureWindowMs = 60 * 1000;
        static constexpr qint64 kRefusalMs = 60 * 1000;
        static constexpr qint64 kPeerLogIntervalMs = 60 * 1000;
        static constexpr int kMaxTrackedPeers = 1024;
        static constexpr qint64 kReadBufferSize = 64 * 1024;

        static void Log(const QString &line);

        static QHostAddress NormalizedPeer(const QHostAddress &address);

        void acceptPending();

        void admit(QTcpSocket *socket);

        bool isAllowed(const QHostAddress &address) const;

        bool isRefused(const QString &peer) const;

        void processInput(Connection *conn);

        void handleRequest(Connection *conn, const QByteArray &plaintext);

        void deliver(quint64 connectionId, quint64 number, const Response &response);

        void sendResponse(Connection *conn, const Response &response);

        void fail(Connection *conn, const QString &reason);

        void closeConnection(Connection *conn);

        void closeAll();

        void stopListening();

        void logPeer(const QString &peer, const QString &line);

        Server *server_;
        QTcpServer *listener_;
        std::shared_ptr<Mailbox> mailbox_;
        Config config_;
        bool listening_ = false;
        QHash<quint64, Connection *> connections_;
        QHash<QString, int> perPeer_;
        QHash<QString, FailureRecord> failures_;
        QHash<QString, qint64> peerLogged_;
        QElapsedTimer clock_;
        quint64 nextId_ = 1;
    };

    Server::Worker::Exchange::~Exchange() {
        if (!answered) answer(Response::Error(500, QStringLiteral("internal"), Server::tr("The request was dropped without an answer")));
    }

    void Server::Worker::Exchange::answer(const Response &response) {
        if (answered.exchange(true)) return;
        // Held while posting, so the worker cannot be destroyed between the check and the post.
        QMutexLocker lock(&mailbox->mutex);
        auto *worker = mailbox->worker;
        if (worker == nullptr) return;
        QMetaObject::invokeMethod(worker, [worker, id = connectionId, n = number, response] { worker->deliver(id, n, response); }, Qt::QueuedConnection);
    }

    Server::Worker::Worker(Server *server)
        : server_(server), listener_(new QTcpServer(this)), mailbox_(std::make_shared<Mailbox>()) {
        mailbox_->worker = this;
        clock_.start();
        connect(listener_, &QTcpServer::newConnection, this, [this] { acceptPending(); });
    }

    Server::Worker::~Worker() {
        QMutexLocker lock(&mailbox_->mutex);
        mailbox_->worker = nullptr;
    }

    void Server::Worker::Log(const QString &line) {
        if (MW_show_log) MW_show_log(QStringLiteral("[API] ") + line);
    }

    // A dual-stack listener reports IPv4 peers as IPv4-mapped IPv6 addresses.
    QHostAddress Server::Worker::NormalizedPeer(const QHostAddress &address) {
        bool isIPv4 = false;
        const auto ipv4 = address.toIPv4Address(&isIPv4);
        return isIPv4 ? QHostAddress(ipv4) : address;
    }

    void Server::Worker::apply(const Config &config) {
        const bool keyValid = config.key.size() == kKeySize;
        const bool sameListener = listening_ && config.enabled && keyValid && config.port == config_.port
                                  && config.lan == config_.lan && config.key == config_.key;
        const bool allowChanged = config.allow != config_.allow;
        config_ = config;

        if (sameListener) {
            if (allowChanged) {
                const auto open = connections_.values();
                for (auto *conn : open) {
                    if (!isAllowed(conn->address)) closeConnection(conn);
                }
            }
            return;
        }

        closeAll();
        stopListening();
        if (!config.enabled) {
            server_->publishStatus(Server::tr("Off"));
            return;
        }
        if (!keyValid) {
            server_->publishStatus(Server::tr("Not listening: the key is missing or invalid"));
            return;
        }

        const auto address = config.lan ? QHostAddress(QHostAddress::Any) : QHostAddress(QHostAddress::LocalHost);
        if (!listener_->listen(address, config.port)) {
            const auto text = listener_->serverError() == QAbstractSocket::AddressInUseError
                                  ? Server::tr("Port %1 is in use").arg(config.port)
                                  : Server::tr("Cannot listen on port %1: %2").arg(config.port).arg(listener_->errorString());
            Log(text);
            server_->publishStatus(text);
            return;
        }
        listening_ = true;
        const auto text = config.lan ? Server::tr("Listening on port %1 (local network)").arg(config.port)
                                     : Server::tr("Listening on %1:%2").arg(QStringLiteral("127.0.0.1")).arg(config.port);
        Log(text);
        server_->publishStatus(text);
    }

    void Server::Worker::stopListening() {
        if (!listening_) return;
        listener_->close();
        listening_ = false;
        Log(Server::tr("Stopped listening"));
    }

    void Server::Worker::acceptPending() {
        while (auto *socket = listener_->nextPendingConnection()) admit(socket);
    }

    void Server::Worker::admit(QTcpSocket *socket) {
        const auto address = NormalizedPeer(socket->peerAddress());
        const auto peer = address.toString();
        QString refusal;
        if (!address.isLoopback() && isRefused(peer)) {
            refusal = Server::tr("Refused %1: too many failed attempts");
        } else if (!isAllowed(address)) {
            refusal = Server::tr("Refused %1: not in the allowed addresses");
        } else if (connections_.size() >= kMaxConnections
                   || (!address.isLoopback() && perPeer_.value(peer) >= kMaxConnectionsPerPeer)) {
            refusal = Server::tr("Refused %1: too many connections");
        }
        if (!refusal.isEmpty()) {
            logPeer(peer, refusal.arg(peer));
            socket->abort();
            socket->deleteLater();
            return;
        }

        auto *conn = new Connection(this);
        conn->id = nextId_++;
        conn->address = address;
        conn->peer = peer;
        conn->socket = socket;
        conn->timer = new QTimer(conn);
        conn->timer->setSingleShot(true);
        socket->setParent(conn);
        // A bounded buffer stops reading from the OS, so a client that sends ahead of its responses is throttled by TCP.
        socket->setReadBufferSize(kReadBufferSize);
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connections_.insert(conn->id, conn);
        ++perPeer_[peer];

        connect(socket, &QTcpSocket::readyRead, conn, [this, conn] { processInput(conn); });
        connect(socket, &QTcpSocket::bytesWritten, conn, [this, conn] {
            if (conn->socket->bytesToWrite() == 0) processInput(conn);
        });
        connect(socket, &QTcpSocket::disconnected, conn, [this, conn] { closeConnection(conn); });
        connect(socket, &QTcpSocket::errorOccurred, conn, [this, conn] { closeConnection(conn); });
        connect(conn->timer, &QTimer::timeout, conn, [this, conn] { closeConnection(conn); });

        conn->serverRandom = RandomBytes(kRandomSize);
        socket->write(conn->serverRandom);
        conn->timer->start(kHelloTimeoutMs);
        processInput(conn);
    }

    bool Server::Worker::isAllowed(const QHostAddress &address) const {
        if (address.isLoopback()) return true;
        if (!config_.lan) return false;
        if (config_.allow.isEmpty()) return true;
        return std::any_of(config_.allow.cbegin(), config_.allow.cend(), [&address](const QPair<QHostAddress, int> &subnet) {
            return address.isInSubnet(subnet.first, subnet.second);
        });
    }

    bool Server::Worker::isRefused(const QString &peer) const {
        const auto it = failures_.constFind(peer);
        return it != failures_.cend() && it->refusedUntil > clock_.elapsed();
    }

    void Server::Worker::processInput(Connection *conn) {
        auto *socket = conn->socket;
        while (!conn->closed) {
            if (conn->phase == Phase::Hello) {
                if (socket->bytesAvailable() < kClientHelloSize) return;
                const auto hello = socket->read(kClientHelloSize);
                if (hello.at(0) != kProtocolVersion) {
                    fail(conn, Server::tr("unsupported protocol version"));
                    return;
                }
                conn->keys = DeriveKeys(config_.key, conn->serverRandom, hello.mid(1));
                conn->phase = Phase::Ready;
                conn->timer->start(kIdleTimeoutMs);
                continue;
            }

            // A request waits until the previous response has left the write buffer.
            if (conn->phase != Phase::Ready || socket->bytesToWrite() > 0) return;
            if (conn->header.isEmpty()) {
                if (socket->bytesAvailable() < kFrameHeaderSize) return;
                conn->header = socket->read(kFrameHeaderSize);
                conn->length = qFromBigEndian<quint32>(conn->header.constData());
                if (conn->length < quint32(kTagSize) || conn->length > kMaxClientFrame) {
                    fail(conn, Server::tr("invalid frame length"));
                    return;
                }
            }
            conn->body += socket->read(qint64(conn->length) - conn->body.size());
            if (conn->body.size() < qsizetype(conn->length)) return;

            if (conn->received == std::numeric_limits<quint64>::max()) {
                closeConnection(conn);
                return;
            }
            QByteArray plaintext;
            const bool opened = OpenFrame(conn->keys.c2s, conn->received, conn->header, conn->body, &plaintext);
            conn->header.clear();
            conn->body.clear();
            if (!opened) {
                fail(conn, Server::tr("a frame failed authentication, most likely the key is wrong"));
                return;
            }
            ++conn->received;
            handleRequest(conn, plaintext);
        }
    }

    void Server::Worker::handleRequest(Connection *conn, const QByteArray &plaintext) {
        Request request;
        QString error;
        if (!ParseRequest(plaintext, &request, &error)) {
            sendResponse(conn, Response::Error(400, QStringLiteral("bad_request"), error));
            return;
        }
        request.peer = conn->peer;
        conn->phase = Phase::Pending;
        conn->timer->stop();
        auto exchange = std::make_shared<Exchange>();
        exchange->mailbox = mailbox_;
        exchange->connectionId = conn->id;
        exchange->number = ++conn->exchanges;
        const Responder respond = [exchange](const Response &response) { exchange->answer(response); };
        auto *server = server_;
        QMetaObject::invokeMethod(server, [server, request, respond] { server->dispatch(request, respond); }, Qt::QueuedConnection);
    }

    void Server::Worker::deliver(quint64 connectionId, quint64 number, const Response &response) {
        auto *conn = connections_.value(connectionId);
        if (conn == nullptr || conn->phase != Phase::Pending || conn->exchanges != number) return;
        sendResponse(conn, response);
        processInput(conn);
    }

    void Server::Worker::sendResponse(Connection *conn, const Response &response) {
        auto plaintext = SerializeResponse(response);
        if (plaintext.size() > qsizetype(kMaxServerFrame) - kTagSize) {
            plaintext = SerializeResponse(Response::Error(500, QStringLiteral("internal"), Server::tr("The response is too large")));
        }
        if (conn->sent == std::numeric_limits<quint64>::max()) {
            closeConnection(conn);
            return;
        }
        conn->socket->write(SealFrame(conn->keys.s2c, conn->sent++, plaintext));
        conn->phase = Phase::Ready;
        conn->timer->start(kIdleTimeoutMs);
    }

    void Server::Worker::fail(Connection *conn, const QString &reason) {
        logPeer(conn->peer, Server::tr("Closed the connection from %1: %2").arg(conn->peer, reason));
        if (!conn->address.isLoopback()) {
            const auto now = clock_.elapsed();
            if (failures_.size() >= kMaxTrackedPeers) {
                failures_.removeIf([now](QHash<QString, FailureRecord>::iterator it) {
                    return it->refusedUntil <= now && (it->recent.isEmpty() || now - it->recent.constLast() >= kFailureWindowMs);
                });
            }
            auto &record = failures_[conn->peer];
            record.recent.removeIf([now](qint64 at) { return now - at >= kFailureWindowMs; });
            record.recent.append(now);
            if (record.recent.size() >= kFailureLimit) {
                record.recent.clear();
                record.refusedUntil = now + kRefusalMs;
            }
        }
        closeConnection(conn);
    }

    void Server::Worker::closeConnection(Connection *conn) {
        if (conn->closed) return;
        conn->closed = true;
        conn->timer->stop();
        connections_.remove(conn->id);
        if (--perPeer_[conn->peer] <= 0) perPeer_.remove(conn->peer);
        QObject::disconnect(conn->socket, nullptr, conn, nullptr);
        conn->socket->abort();
        conn->deleteLater();
    }

    void Server::Worker::closeAll() {
        const auto open = connections_.values();
        for (auto *conn : open) closeConnection(conn);
    }

    void Server::Worker::logPeer(const QString &peer, const QString &line) {
        const auto now = clock_.elapsed();
        if (const auto it = peerLogged_.constFind(peer); it != peerLogged_.cend() && now - it.value() < kPeerLogIntervalMs) return;
        if (peerLogged_.size() >= kMaxTrackedPeers) {
            peerLogged_.removeIf([now](QHash<QString, qint64>::iterator it) { return now - it.value() >= kPeerLogIntervalMs; });
        }
        peerLogged_.insert(peer, now);
        Log(line);
    }

    Server *Server::instance() {
        static auto *server = new Server();
        return server;
    }

    Server::Server() : QObject(QCoreApplication::instance()), status_(tr("Off")) {
        if (auto *app = QCoreApplication::instance(); app != nullptr) connect(app, &QCoreApplication::aboutToQuit, this, &Server::shutdown);
    }

    Server::~Server() {
        shutdown();
    }

    void Server::setDispatcher(Dispatcher dispatcher) {
        dispatcher_ = std::move(dispatcher);
    }

    void Server::apply(const Config &config) {
        if (stopped_) return;
        if (worker_ == nullptr) {
            if (!config.enabled) {
                publishStatus(tr("Off"));
                return;
            }
            thread_ = new QThread(this);
            thread_->setObjectName(QStringLiteral("RemoteApi"));
            worker_ = new Worker(this);
            worker_->moveToThread(thread_);
            // QThread runs deferred deletes as it finishes, so the worker and its sockets die on their own thread.
            connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
            thread_->start();
        }
        QMetaObject::invokeMethod(worker_, [worker = worker_, config] { worker->apply(config); }, Qt::QueuedConnection);
    }

    void Server::shutdown() {
        if (stopped_) return;
        stopped_ = true;
        if (thread_ != nullptr) {
            thread_->quit();
            thread_->wait();
            worker_ = nullptr;
        }
        publishStatus(tr("Off"));
    }

    QString Server::statusText() const {
        QMutexLocker lock(&statusMutex_);
        return status_;
    }

    void Server::publishStatus(const QString &text) {
        {
            QMutexLocker lock(&statusMutex_);
            if (status_ == text) return;
            status_ = text;
        }
        QMetaObject::invokeMethod(this, [this, text] { emit statusChanged(text); }, Qt::QueuedConnection);
    }

    void Server::dispatch(const Request &request, const Responder &respond) {
        if (stopped_ || !dispatcher_) {
            respond(Response::Error(503, QStringLiteral("exiting"), tr("qThrone is shutting down")));
            return;
        }
        dispatcher_(request, respond);
    }
}
