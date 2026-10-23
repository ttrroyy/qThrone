#pragma once
#include "include/ui/utils/ProfilesTableModel.h"
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainter>
#include <QApplication>
#include <QDateTime>
#include <cmath>

class TestResultDelegate final : public QStyledItemDelegate {
public:
    explicit TestResultDelegate(QTableView *table) : QStyledItemDelegate(table) {
        clock.start();
        auto *timer = new QTimer(this);
        timer->setInterval(50);
        connect(timer, &QTimer::timeout, table->viewport(), [this, table] {
            if (!active || !table->isVisible()) return;
            active = false; table->viewport()->update();
        });
        timer->start();
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        const int state = index.data(ProfilesTableModel::TestStateRole).toInt();
        if (!state || (state == 2 && !index.data(ProfilesTableModel::TestCountryRole).toString().isEmpty())) return;
        active = true;
        const QString text = index.data().toString();
        const int side = qMax(8, qMin(option.fontMetrics.height(), option.rect.height()-4));
        const int left = option.rect.left()+4+(text.isEmpty() ? 0 : option.fontMetrics.horizontalAdvance(text)+6);
        const QRectF box(left, option.rect.center().y()-side/2., side, side);
        painter->save(); painter->setClipRect(option.rect); painter->setRenderHint(QPainter::Antialiasing);
        const auto color = option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text);
        painter->setPen(QPen(color, qBound(1., side / 11., 1.5), Qt::SolidLine, Qt::RoundCap));
        if (state == 1) {
            const qint64 since = index.data(ProfilesTableModel::TestSinceRole).toLongLong();
            const qint64 spun = since > 0 ? QDateTime::currentMSecsSinceEpoch() - since : clock.elapsed();
            painter->drawArc(box.adjusted(1,1,-1,-1), -int(spun%1000)*5760/1000, 250*16);
        } else {
            painter->drawArc(box.adjusted(1,1,-1,-1), -45*16, 270*16);
            const QPointF center=box.center();
            const double a=(-90+55*std::sin(clock.elapsed()*6.283185307179586/1600.))*3.141592653589793/180.;
            painter->drawLine(center,center+QPointF(std::cos(a)*side*.28,std::sin(a)*side*.28));
            painter->drawPoint(center);
        }
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
        auto size=QStyledItemDelegate::sizeHint(option,index);
        if (index.data(ProfilesTableModel::TestStateRole).toInt()) size.rwidth()+=22;
        return size;
    }
private:
    QElapsedTimer clock;
    mutable bool active = false;
};
