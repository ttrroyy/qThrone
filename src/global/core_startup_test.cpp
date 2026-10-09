#include "include/global/Configs.hpp"
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUuid>

// Run inside the packaged GUI executable so the core's parent identity check
// verifies the same executable name and directory as a normal application start.
int TestCoreStartup() {
    qInstallMessageHandler(nullptr);
    const auto path = Configs::FindCoreRealPath();
    if (!QFileInfo::exists(path)) {
        qCritical() << "Packaged core is missing:" << path;
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QString socketName = "qthrone-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef Q_OS_UNIX
    // macOS runner TMPDIR plus a UUID exceeds Qt 6.4's Unix socket path limit.
    // Keep the socket in a private directory with a short absolute path.
    QTemporaryDir socketDirectory("/tmp/qthrone-test-XXXXXX");
    if (!socketDirectory.isValid()) {
        qCritical() << "Cannot create the core startup test socket directory";
        return 1;
    }
    socketName = socketDirectory.filePath("core");
#endif
    if (!server.listen(socketName)) {
        qCritical() << "Core startup test socket:" << server.errorString();
        return 1;
    }
    QProcess core;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("THRONE_GUARD");
    environment.insert("THRONE_CORE_SOCKET", server.fullServerName());
    core.setProcessEnvironment(environment);
    core.setProcessChannelMode(QProcess::MergedChannels);
    core.start(path, QStringList{});
    const bool started = core.waitForStarted(10000);
    const bool connected = started && server.waitForNewConnection(20000);
    auto *socket = connected ? server.nextPendingConnection() : nullptr;
    QByteArray output = core.readAll();
    QElapsedTimer deadline;
    deadline.start();
    while (socket && core.state() == QProcess::Running && deadline.elapsed() < 5000 &&
           !output.contains("Core Has Successfully Connected to qThrone!")) {
        core.waitForReadyRead(250);
        output += core.readAll();
    }
    const bool ok = socket && core.state() == QProcess::Running &&
                    output.contains("Core Has Successfully Connected to qThrone!");
    if (!ok) qCritical() << "Packaged core startup failed:" << core.errorString() << output;
    core.terminate();
    if (!core.waitForFinished(5000)) {
        core.kill();
        core.waitForFinished(5000);
    }
    delete socket;
    qInfo() << "Packaged core startup and IPC:" << (ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
