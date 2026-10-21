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
class QCheckBox;

class EditWdttPlus : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditWdttPlus(QWidget *parent = nullptr);
    ~EditWdttPlus() override;
    void onStart(std::shared_ptr<Configs::Profile> ent) override;
    bool onEnd() override;
    // The editor's text for one verdict of WDTT Plus's own hash check.
    static QString HashVerdict(const QString &status);
private:
    std::shared_ptr<Configs::Profile> ent;
    QFormLayout *form;
    QLineEdit *password;
    QComboBox *hashSource;
    QSpinBox *autoHashCount;
    std::array<QLineEdit *, 4> hashes;
    std::array<QWidget *, 4> hashRows;
    std::array<QLabel *, 4> hashStatus;
    QPushButton *checkHashes;
    QProcess *hashProcess = nullptr;
    QTimer *hashTimeout = nullptr;
    std::unique_ptr<QTemporaryDir> hashCheckDirectory;
    QByteArray hashCheckOutput;
    // Field slot of each hash being checked, in the order the client numbers them.
    QList<int> checking;
    bool hashCheckStopped = false;
    void startHashCheck();
    void stopHashCheck();
    void consumeHashCheckOutput();
    void finishHashCheck();
    QComboBox *workers;
    int maxWorkers = 0;
    void updateWorkers(int requested = -1);
    void updateSource();
    QComboBox *captcha;
    QCheckBox *tcpOnly;
    QCheckBox *rtMasque;
    QCheckBox *serverBootstrap;
    QCheckBox *preflight;
    QLineEdit *turnSni;
    QComboBox *fingerprint;
    QLineEdit *clientIds;
    QSpinBox *wgPort;
    QSpinBox *localPort;
    QLineEdit *device;
};
