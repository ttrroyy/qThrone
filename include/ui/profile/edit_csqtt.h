#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <array>
#include <memory>
#include "profile_editor.h"
class QFormLayout;
class QLabel;
class QProcess;
class QPushButton;
class QTemporaryDir;
class QTimer;

class EditCSQTT : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditCSQTT(QWidget *parent = nullptr);
    ~EditCSQTT() override;
    void onStart(std::shared_ptr<Configs::Profile> ent) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QFormLayout *form;
    QLineEdit *password;
    QComboBox *hashSource;
    QSpinBox *autoHashCount;
    std::array<QLineEdit *, 6> hashes;
    std::array<QWidget *, 6> hashRows;
    std::array<QLabel *, 6> hashStatus;
    QPushButton *checkHashes;
    QProcess *hashProcess = nullptr;
    QTimer *hashTimeout = nullptr;
    std::unique_ptr<QTemporaryDir> hashCheckDirectory;
    QByteArray hashCheckOutput;
    // Field slot and hash of each hash being checked.
    QList<QPair<int, QString>> checking;
    bool hashCheckStopped = false;
    void startHashCheck();
    void stopHashCheck();
    void consumeHashCheckOutput();
    void finishHashCheck();
    QComboBox *workers;
    void updateWorkers(int requested = -1);
    void updateSource();
    QComboBox *transport;
    QComboBox *obfs;
    QComboBox *authorization;
    QComboBox *captcha;
    QComboBox *srtp;
    QComboBox *fingerprint;
    QLineEdit *clientIds;
    QLineEdit *customTurnIps;
    QLineEdit *token;
    QLineEdit *device;
};
