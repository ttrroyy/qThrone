#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include "profile_editor.h"
class EditSSTP : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditSSTP(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QLineEdit *username, *password, *sni;
    QCheckBox *insecure;
};
