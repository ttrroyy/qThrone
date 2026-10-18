#pragma once
#include <QJsonObject>

class QWidget;

// «Выходной IP и прокси» of a WDTT Plus server: where its clients leave to the internet.
void OpenWdttPlusExit(QWidget *parent, const QJsonObject &server);
