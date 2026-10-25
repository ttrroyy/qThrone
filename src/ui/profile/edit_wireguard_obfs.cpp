#include "include/ui/profile/edit_wireguard_obfs.h"

#include <QCheckBox>

#include "include/global/GuiUtils.hpp"
#include "include/global/Utils.hpp"

EditWireguardObfs::EditWireguardObfs(QWidget *parent, const std::shared_ptr<Configs::Profile> &_ent)
    : QDialog(parent)
    , ui(new Ui::EditWireguardObfs)
{
    ui->setupUi(this);
    ent = _ent;
    auto outbound = ent->Wireguard();

    ui->type->setCurrentIndex(outbound->obfs_type == "wg-obfuscator" ? 1 : outbound->obfs_type == "wgobfs" ? 2 : 0);
    ui->key->setText(outbound->obfs_key);
    ui->masking->setCurrentText(outbound->obfs_masking.isEmpty() ? QStringLiteral("NONE") : outbound->obfs_masking);
    ui->max_dummy->setText(outbound->obfs_max_dummy >= 0 ? Int2String(outbound->obfs_max_dummy) : QString());
    ui->obfuscate_bytes->setText(outbound->obfs_obfuscate_bytes > 0 ? Int2String(outbound->obfs_obfuscate_bytes) : QString());
    ui->media_pt->setText(outbound->obfs_media_pt > 0 ? Int2String(outbound->obfs_media_pt) : QString());
    ui->media_ssrc->setText(outbound->obfs_media_ssrc > 0 ? QString::number(outbound->obfs_media_ssrc) : QString());
    ui->media_clock->setText(outbound->obfs_media_clock > 0 ? Int2String(outbound->obfs_media_clock) : QString());

    connect(ui->type, &QComboBox::currentIndexChanged, this, &EditWireguardObfs::updateFields);
    connect(ui->masking, &QComboBox::currentIndexChanged, this, &EditWireguardObfs::updateFields);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateFields();

    ADD_ASTERISK(this)
    adjustSize();
}

EditWireguardObfs::~EditWireguardObfs()
{
    delete ui;
}

void EditWireguardObfs::updateFields() {
    const bool any = ui->type->currentIndex() != 0;
    const bool clusterM = ui->type->currentIndex() == 1;
    const bool media = clusterM && ui->masking->currentText() == "MEDIA";
    for (auto *w : std::initializer_list<QWidget *>{ui->key, ui->key_l}) w->setEnabled(any);
    for (auto *w : std::initializer_list<QWidget *>{ui->masking, ui->masking_l, ui->max_dummy, ui->max_dummy_l, ui->obfuscate_bytes, ui->obfuscate_bytes_l})
        w->setEnabled(clusterM);
    for (auto *w : std::initializer_list<QWidget *>{ui->media_pt, ui->media_pt_l, ui->media_ssrc, ui->media_ssrc_l, ui->media_clock, ui->media_clock_l})
        w->setEnabled(media);
}

void EditWireguardObfs::accept() {
    auto outbound = ent->Wireguard();
    const int type = ui->type->currentIndex();
    if (type != 0 && ui->key->text().trimmed().isEmpty()) {
        MessageBoxWarning(tr("Obfuscation"), tr("Enter the obfuscator key."));
        return;
    }
    outbound->obfs_type = type == 1 ? QStringLiteral("wg-obfuscator") : type == 2 ? QStringLiteral("wgobfs") : QString();
    outbound->obfs_key = type == 0 ? QString() : ui->key->text().trimmed();
    outbound->obfs_masking = type == 1 && ui->masking->currentText() != "NONE" ? ui->masking->currentText() : QString();
    outbound->obfs_max_dummy = type == 1 && !ui->max_dummy->text().trimmed().isEmpty() ? ui->max_dummy->text().trimmed().toInt() : -1;
    outbound->obfs_obfuscate_bytes = type == 1 ? ui->obfuscate_bytes->text().trimmed().toInt() : 0;
    const bool media = outbound->obfs_masking == "MEDIA";
    outbound->obfs_media_pt = media ? ui->media_pt->text().trimmed().toInt() : 0;
    outbound->obfs_media_ssrc = media ? ui->media_ssrc->text().trimmed().toUInt(nullptr, 0) : 0;
    outbound->obfs_media_clock = media ? ui->media_clock->text().trimmed().toInt() : 0;
    // AmneziaWG rewrites the same header bytes the obfuscators work on.
    if (type != 0) outbound->enable_amnezia = false;
    QDialog::accept();
}
