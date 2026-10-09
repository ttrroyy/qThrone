#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <array>
#include "profile_editor.h"

class EditQWDTT : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditQWDTT(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile> ent) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QComboBox *mode;
    QLineEdit *password;
    std::array<QLineEdit *, 4> hashes;
    QSpinBox *rawPort;
    QComboBox *workers;
    void updateWorkers(int requested = -1);
    QComboBox *transport;
    QComboBox *obfs;
    QComboBox *authorization;
    QComboBox *captcha;
    QComboBox *dns;
    QLineEdit *device;
};
