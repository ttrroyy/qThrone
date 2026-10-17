#pragma once
#include <QLineEdit>
#include <QMap>
#include <QSpinBox>
#include <QWidget>
#include "profile_editor.h"

class EditS3Bypass : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditS3Bypass(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QMap<QString, QLineEdit *> storage;
    QMap<QString, QSpinBox *> tuning;
};
