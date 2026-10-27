#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QWidget>
#include "profile_editor.h"

class EditOpenFluxKey : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditOpenFluxKey(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QComboBox *transport;
    QLineEdit *document, *key;
    QCheckBox *e2e;
};
