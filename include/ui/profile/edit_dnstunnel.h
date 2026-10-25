#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QWidget>
#include "profile_editor.h"
class QFormLayout;

// DNSTT, VayDNS, SlipStream, MasterDNS, CottenDNS and Soar: one editor, fields by transport.
class EditDnsTunnel : public QWidget, public ProfileEditor {
    Q_OBJECT
public:
    explicit EditDnsTunnel(QWidget *parent = nullptr);
    void onStart(std::shared_ptr<Configs::Profile>) override;
    bool onEnd() override;
private:
    std::shared_ptr<Configs::Profile> ent;
    QString kind;
    QFormLayout *form;
    QLineEdit *domain, *pubkey, *mkey, *idleTimeout, *keepalive, *user, *password, *soarCountry;
    QPlainTextEdit *cert, *resolvers, *privateKey;
    QComboBox *encryption, *recordType, *congestion, *backend, *method, *soarTransport;
    QCheckBox *dnsttCompat, *authoritative, *soarDiscovery;
    QSpinBox *clientIdSize, *keepAliveInterval;
    QLabel *note;
    void setRowVisible(QWidget *field, bool visible);
    void updateFields();
};
