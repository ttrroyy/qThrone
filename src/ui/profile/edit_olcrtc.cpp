#include "include/ui/profile/edit_olcrtc.h"
#include "include/configs/outbounds/olcrtc.h"
#include <QFormLayout>
#include <QLabel>
EditOLCRTC::EditOLCRTC(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    provider = new QComboBox(this); provider->addItem("Jitsi", "jitsi"); provider->addItem(QStringLiteral("Телемост"), "telemost"); provider->addItem("WB Stream", "wbstream");
    transport = new QComboBox(this); for (const auto *t : {"datachannel","vp8channel","seichannel","videochannel"}) transport->addItem(t,t);
    room = new QLineEdit(this); room->setPlaceholderText("https://meet.example.org/room");
    key = new QLineEdit(this); key->setEchoMode(QLineEdit::Normal);
    token = new QLineEdit(this); token->setEchoMode(QLineEdit::Normal);
    dns = new QLineEdit("8.8.8.8:53",this); channel = new QLineEdit(this); device = new QLineEdit(this);
    form->addRow(QStringLiteral("Сервис"),provider); form->addRow(QStringLiteral("Транспорт"),transport);
    form->addRow(QStringLiteral("Комната / ссылка"),room); form->addRow(QStringLiteral("Ключ (64 HEX)"),key);
    form->addRow(QStringLiteral("Токен сервиса"),token); form->addRow(QStringLiteral("Канал комнаты"),channel);
    form->addRow("DNS",dns); form->addRow(QStringLiteral("Идентификатор устройства"),device);
    const auto add = [&](const QString &id, const QString &label, int min, int max, int value) {
        auto *spin = new QSpinBox(this); spin->setRange(min,max); spin->setValue(value); numbers[id]=spin;
        auto *text = new QLabel(label,this); form->addRow(text,spin); fields[id]=text;
    };
    add("vp8-fps","VP8: кадров/с",1,120,30); add("vp8-batch",QStringLiteral("VP8: кадров в пакете"),1,64,64);
    add("fps","SEI: кадров/с",1,120,30); add("batch",QStringLiteral("SEI: кадров в пакете"),1,64,64);
    add("frag",QStringLiteral("SEI: размер фрагмента"),1,65535,900); add("ack-ms",QStringLiteral("SEI: ожидание ACK, мс"),1,60000,2000);
    add("video-w",QStringLiteral("Видео: ширина"),1,4096,1080); add("video-h",QStringLiteral("Видео: высота"),1,4096,1080);
    add("video-fps",QStringLiteral("Видео: кадров/с"),1,120,30); add("video-qr-size",QStringLiteral("QR: размер фрагмента (0 — авто)"),0,65535,0);
    add("video-tile-module",QStringLiteral("Tile: размер модуля"),1,270,4); add("video-tile-rs",QStringLiteral("Tile: восстановление, %"),0,200,0);
    codec = new QComboBox(this); codec->addItems({"qrcode","tile"}); recovery = new QComboBox(this); recovery->addItems({"low","medium","high","highest"});
    form->addRow(QStringLiteral("Кодек видео"),codec); form->addRow(QStringLiteral("Восстановление QR"),recovery);
    auto *note=new QLabel(QStringLiteral("Только TCP. DNS внутри туннеля использует TCP. Параметры транспорта должны совпадать с сервером."),this); note->setWordWrap(true); form->addRow(note);
    connect(provider,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{
        const auto p=provider->currentData().toString(), t=transport->currentData().toString();
        if ((p=="telemost" && (t=="datachannel" || t=="seichannel")) || (p=="wbstream" && t=="datachannel")) transport->setCurrentIndex(1);
        updateFields();
    });
    connect(transport,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateFields();}); updateFields();
}
void EditOLCRTC::updateFields() {
    const auto t=transport->currentData().toString();
    for (auto it=numbers.begin();it!=numbers.end();++it) {
        const bool visible=t=="videochannel" ? it.key().startsWith("video-") : t=="vp8channel" ? it.key().startsWith("vp8-") : t=="seichannel" ? !it.key().contains('-') || it.key()=="ack-ms" : false;
        it.value()->setVisible(visible); fields[it.key()]->setVisible(visible);
    }
    codec->setEnabled(t=="videochannel"); recovery->setEnabled(t=="videochannel");
}
void EditOLCRTC::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent=std::move(profile); const auto *o=dynamic_cast<Configs::olcrtc*>(ent->outbound.get()); if(!o)return;
    provider->setCurrentIndex(provider->findData(o->provider)); transport->setCurrentIndex(transport->findData(o->transport));
    room->setText(o->room); key->setText(o->key); token->setText(o->token); channel->setText(o->channel); dns->setText(o->dns); device->setText(o->deviceID);
    for(auto it=numbers.begin();it!=numbers.end();++it) if(o->options.contains(it.key()))it.value()->setValue(o->options[it.key()].toVariant().toInt());
    codec->setCurrentText(o->options["video-codec"].toString("qrcode")); recovery->setCurrentText(o->options["video-qr-recovery"].toString("low")); updateFields();
}
bool EditOLCRTC::onEnd() {
    auto *o=dynamic_cast<Configs::olcrtc*>(ent->outbound.get()); if(!o)return false;
    Configs::olcrtc updated; updated.name=o->name; updated.provider=provider->currentData().toString(); updated.transport=transport->currentData().toString();
    updated.room=room->text().trimmed(); updated.key=key->text().trimmed(); updated.token=token->text().trimmed(); updated.channel=channel->text().trimmed(); updated.dns=dns->text().trimmed(); updated.deviceID=device->text().trimmed();
    for(auto it=numbers.begin();it!=numbers.end();++it) if(it.value()->isVisibleTo(this))updated.options[it.key()]=it.value()->value();
    if(updated.transport=="videochannel"){ updated.options["video-codec"]=codec->currentText(); updated.options["video-qr-recovery"]=recovery->currentText(); }
    if(const auto error=updated.Validate();!error.isEmpty()){MessageBoxWarning("olcRTC",error);return false;}
    if(updated.deviceID.isEmpty())updated.deviceID=o->deviceID;
    return o->ParseFromJson(updated.ExportToJson());
}
