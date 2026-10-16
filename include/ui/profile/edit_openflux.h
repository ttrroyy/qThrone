#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QWidget>
#include "profile_editor.h"
class EditOpenFlux : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditOpenFlux(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QLineEdit *secret, *context;
    // One field per document carrier; direct uses the dialog's address and port.
    QMap<QString, QLineEdit *> carriers;
    QMap<QString, QLabel *> carrierLabels;
};
