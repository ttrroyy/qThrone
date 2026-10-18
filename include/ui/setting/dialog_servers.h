#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

class QWidget;

// The menu of a deployed server, by its type: qWDTT users and subscriptions, the CSQTT web
// panel, WDTT Plus clients, exit IP and backups; "Управление сервером" opens the deploy editor.
// current() returns the server as last saved; editor() opens the deploy editor for it.
void OpenServerMenu(QWidget *parent, const std::function<QJsonObject()> &current, const std::function<void()> &editor);

namespace ServerLinks {
    // url.QueryEscape of Go: the encoding of the original apps' links.
    QString QueryEscape(const QString &value);
    // qwdtt://config?name&peer&hashes&workers=18&port=9000&pass, as the original admin tab builds it.
    QString Qwdtt(const QJsonObject &server, const QString &name, const QString &password, const QStringList &hashes);
    // wdtt://<server>:<dtls>:<wg>:9000:<password>:<hashes>, the legacy WDTT form of a qWDTT access.
    QString LegacyWdtt(const QJsonObject &server, const QString &password, const QStringList &hashes);
    // The deploy password is the owner's unlimited access, not one of the server's users.
    bool IsOwnerAccess(const QJsonObject &server, const QString &password);
}
