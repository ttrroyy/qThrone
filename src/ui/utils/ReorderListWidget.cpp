#include "include/ui/utils/ReorderListWidget.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>

namespace {
    constexpr auto REORDER_ROW_MIME_TYPE = "application/x-rawline-row";
    constexpr int REORDER_INDICATOR_THICKNESS = 2;
    constexpr int REORDER_INDICATOR_INSET = 4;

    int reorderRowUnderCursor(const ReorderListWidget *list) {
        const QPoint global = QCursor::pos();
        auto *under = QApplication::widgetAt(global);
        if (under == nullptr || (under != list->viewport() && !list->viewport()->isAncestorOf(under))) return -1;
        return list->indexAt(list->viewport()->mapFromGlobal(global)).row();
    }
}

ReorderListWidget::ReorderListWidget(QWidget *parent) : QListWidget(parent) {
    setDragEnabled(false);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->installEventFilter(this);
    // Scrolling moves rows under a still cursor without any enter or leave event.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { RefreshHover(); });
}

void ReorderListWidget::RefreshHover() {
    if (dragging) return;
    hoveredRow = reorderRowUnderCursor(this);
    emit hoveredRowChanged(hoveredRow);
}

void ReorderListWidget::setHoveredRow(const int row) {
    if (row == hoveredRow) return;
    hoveredRow = row;
    emit hoveredRowChanged(row);
}

bool ReorderListWidget::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::ChildAdded:
    case QEvent::ChildPolished:
        if (watched == viewport()) {
            if (auto *child = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child())) child->installEventFilter(this);
            // A rebuilt row needs its actions applied once it has its geometry.
            if (event->type() == QEvent::ChildAdded) QTimer::singleShot(0, this, [this] { RefreshHover(); });
        }
        break;
    case QEvent::Enter:
    case QEvent::Leave:
        // From the cursor, not from the receiver: a fast move delivers the old row's Leave with the cursor on the next row.
        if (!dragging) setHoveredRow(reorderRowUnderCursor(this));
        break;
    default:
        break;
    }
    return QListWidget::eventFilter(watched, event);
}

void ReorderListWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        pressPos = event->position().toPoint();
        pressRow = indexAt(pressPos).row();
    }
    QListWidget::mousePressEvent(event);
}

void ReorderListWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || pressRow < 0 || count() < 2) {
        QListWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - pressPos).manhattanLength() < QApplication::startDragDistance()) {
        QListWidget::mouseMoveEvent(event);
        return;
    }

    auto *mime = new QMimeData;
    mime->setData(REORDER_ROW_MIME_TYPE, QByteArray::number(pressRow));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (auto *rowWidget = itemWidget(item(pressRow))) {
        drag->setPixmap(rowWidget->grab());
        drag->setHotSpot(pressPos - rowWidget->pos());
    }
    dragging = true;
    setHoveredRow(-1);
    // Never MoveAction: Qt deletes the source row itself when exec() returns it.
    drag->exec(Qt::CopyAction);
    dragging = false;
    // The drag ate the release, so a selecting view would stay in DragSelectingState.
    setState(NoState);
    pressRow = -1;
    if (indicatorRow != -1) {
        indicatorRow = -1;
        viewport()->update();
    }
    RefreshHover();
}

int ReorderListWidget::dropRow(const QPoint &pos) const {
    if (count() == 0) return 0;

    const auto target = indexAt(pos);
    if (!target.isValid()) return pos.y() <= visualItemRect(item(0)).top() ? 0 : count();

    const int row = target.row();
    return pos.y() > visualItemRect(item(row)).center().y() ? row + 1 : row;
}

void ReorderListWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->source() != this || !event->mimeData()->hasFormat(REORDER_ROW_MIME_TYPE)) {
        event->ignore();
        return;
    }
    if (const int row = dropRow(event->position().toPoint()); row != indicatorRow) {
        indicatorRow = row;
        viewport()->update();
    }
    event->acceptProposedAction();
}

void ReorderListWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (event->source() != this || !event->mimeData()->hasFormat(REORDER_ROW_MIME_TYPE)) {
        event->ignore();
        return;
    }
    if (const int row = dropRow(event->position().toPoint()); row != indicatorRow) {
        indicatorRow = row;
        viewport()->update();
    }
    event->acceptProposedAction();
}

void ReorderListWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    if (indicatorRow != -1) {
        indicatorRow = -1;
        viewport()->update();
    }
    QListWidget::dragLeaveEvent(event);
}

void ReorderListWidget::dropEvent(QDropEvent *event) {
    if (indicatorRow != -1) {
        indicatorRow = -1;
        viewport()->update();
    }
    if (event->source() != this || !event->mimeData()->hasFormat(REORDER_ROW_MIME_TYPE)) {
        event->ignore();
        return;
    }

    const int from = event->mimeData()->data(REORDER_ROW_MIME_TYPE).toInt();
    int to = dropRow(event->position().toPoint());
    if (from < to) --to;
    if (to >= count()) to = count() - 1;

    event->acceptProposedAction();
    if (from >= 0 && from < count() && to >= 0 && from != to) emit reorderRequested(from, to);
}

void ReorderListWidget::paintEvent(QPaintEvent *event) {
    QListWidget::paintEvent(event);
    if (indicatorRow < 0 || count() == 0) return;

    const int y = indicatorRow >= count() ? visualItemRect(item(count() - 1)).bottom()
                                          : visualItemRect(item(indicatorRow)).top();

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(palette().color(QPalette::Highlight), REORDER_INDICATOR_THICKNESS));
    painter.drawLine(REORDER_INDICATOR_INSET, y, viewport()->width() - REORDER_INDICATOR_INSET, y);
}
