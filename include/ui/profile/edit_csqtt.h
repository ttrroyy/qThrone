#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <array>
#include "profile_editor.h"

class EditCSQTT : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditCSQTT(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile> ent) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QLineEdit *password;
    std::array<QLineEdit *, 6> hashes;
    QComboBox *workers;
    void updateWorkers(int requested = -1);
    QComboBox *transport;
    QComboBox *obfs;
    QComboBox *authorization;
    QComboBox *captcha;
    QLineEdit *device;
};
