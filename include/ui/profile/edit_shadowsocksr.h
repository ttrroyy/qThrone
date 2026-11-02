#pragma once

#include <QComboBox>
#include <QLineEdit>
#include "profile_editor.h"

class EditShadowsocksR : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditShadowsocksR(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile> profile) override;
    bool onEnd() override;
    void bindValidation(Validation::Form &form) override;

private:
    std::shared_ptr<Configs::Profile> ent;
    QComboBox *method, *protocol, *obfs;
    QLineEdit *password, *protocolParam, *obfsParam;
};
