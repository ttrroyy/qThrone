#pragma once

#include <QDialog>
#include "ui_edit_wireguard_obfs.h"
#include "include/database/entities/Profile.h"

namespace Ui {
class EditWireguardObfs;
}

// The obfuscator in front of a WireGuard server: wg-obfuscator (ClusterM, Phobos) or xt_wgobfs.
class EditWireguardObfs : public QDialog
{
    Q_OBJECT

public:
    EditWireguardObfs(QWidget *parent, const std::shared_ptr<Configs::Profile> &_ent);

    ~EditWireguardObfs() override;

public slots:
    void accept() override;

private:
    Ui::EditWireguardObfs *ui;
    std::shared_ptr<Configs::Profile> ent;
    void updateFields();
};
