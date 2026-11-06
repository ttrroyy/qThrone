#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QWidget>
#include "profile_editor.h"
class EditFreeTurn : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditFreeTurn(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QComboBox *hashSource, *provider, *tunnelMode, *relayTransport, *obfProfile, *dnsMode;
    QPlainTextEdit *links, *wgConfig;
    QLineEdit *obfKey, *clientId, *dnsServers, *turnHost;
    QSpinBox *threads, *streamsPerCred, *obfTiming;
    QCheckBox *bond, *manualCaptcha;
};
