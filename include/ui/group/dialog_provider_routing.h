#pragma once
#include "include/database/entities/Group.h"
class QWidget;
bool EditProviderRouting(Configs::SubscriptionOptions &options, QWidget *parent);
bool SaveProviderRoutingPreviews(const QString &directory);
