#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QProcess>
#include <QQueue>
#include <functional>

class RelayTask : public QObject {
  public:
    using Handler = std::function<void(const QJsonObject &)>;
    explicit RelayTask(QObject *parent = nullptr);
    ~RelayTask() override;
    void start(const QJsonObject &request, Handler handler);
    void approve(bool accepted);
    void cancel();
    bool running() const;

  private:
    QProcess process;
    QByteArray pending;
    QByteArray requestBytes;
    Handler handler;
    bool failed = false;
    bool finishing = false;
    quint64 generation = 0;
    QQueue<std::pair<QJsonObject, Handler>> queue;
};

QStringList RelayAccountHashes(int count, QString *error);
// The hashes chosen for this server, or the first [count] created ones when none are chosen.
QStringList RelayAutoHashes(const QString &type, const QString &server, int port, int count, QString *error);
void RelayPickHashes(QWidget *parent, const QString &type, const QString &server, int port, int limit);
QString RelayAccountToken(QString *error);
// The bound MAX account: {token, group}; an error when none is bound.
QJsonObject RelayMaxAccount(QString *error);
bool ConfirmMaxRisk(QWidget *parent);
bool ResetQWDTTHashSources();
bool SaveVKRelayPreviews(const QString &directory);

class DialogVKRelay : public QDialog {
  public:
    explicit DialogVKRelay(QWidget *parent = nullptr);
};
