#pragma once

#include <QWidget>
#include <QPainter>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDate>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QHBoxLayout>
#include <QTabBar>
#include <QToolButton>
#include <QTextBrowser>
#include <QTextDocument>
#include <QShowEvent>
#include <QHideEvent>

class SeasonalDecorationSpace final : public QWidget {
public:
    explicit SeasonalDecorationSpace(QWidget *parent) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
        setFixedWidth(0);
    }
    QSize sizeHint() const override { return {0, 0}; }
};

// Approved transparent artwork, animated independently of the proxy/core.
// The passive overlay stays outside the splitter and never intercepts clicks.
class SeasonalDecoration final : public QWidget {
public:
    explicit SeasonalDecoration(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        clock.start();
        timer.setInterval(100);
        connect(&timer, &QTimer::timeout, this, [this] { update(); });
        calendarTimer.setInterval(60000);
        connect(&calendarTimer, &QTimer::timeout, this, [this] {
            if (selection == "auto" && themeForDate(QDate::currentDate()) != theme) updateBounds();
        });
        calendarTimer.start();
    }
    static QStringList manualThemes() {
        return {"halloween", "newyear", "kupala", "patrick", "christmas", "easter", "valentine", "maslenitsa"};
    }
    static QString themeForDate(const QDate &date) {
        if (!date.isValid()) return {};
        const int month = date.month(), day = date.day();
        // Exact dates override seasonal defaults, including the cross-year Christmas interval.
        if (month == 10 && day == 30) return "remembrance";
        if (month == 5 && (day == 8 || day == 9)) return "may_remembrance";
        if ((month == 12 && day >= 24) || (month == 1 && day <= 7)) return "christmas";
        if (month == 2 && day == 14) return "valentine";
        if (month == 3 && day == 17) return "patrick";
        if (month == 3 && day >= 8 && day <= 14) return "maslenitsa";
        if (month >= 9 && month <= 11) return "halloween";
        if (month == 12 || month <= 2) return "newyear";
        if (month >= 3 && month <= 5) return "easter";
        return "kupala";
    }
    QString currentTheme() const { return theme; }
    QString currentVariant() const { return variant; }
    bool hasArtwork() const { return !atlas.isNull(); }
    QSize sizeHint() const override { return {190, 64}; }
    QSize minimumSizeHint() const override { return {0, 0}; }
    void setAnchors(QWidget *space, QWidget *divider, int filterWidth, QTextBrowser *status = nullptr) {
        anchor = space; bottom = divider; notice = status;
        rightInset = filterWidth + 6;
        space->installEventFilter(this); divider->installEventFilter(this);
        parentWidget()->installEventFilter(this);
        if (notice) notice->installEventFilter(this);
        queueBounds();
    }
    void setNoticePriority(bool) {
        // Presence alone does not hide artwork: updateBounds measures whether both fit.
        queueBounds();
    }
    void setDecoration(const QString &value) {
        selection = value == "none" || value == "auto" || manualThemes().contains(value) ? value : "auto";
        updateBounds();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
            event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest)
            queueBounds();
        return false;
    }
    void showEvent(QShowEvent *event) override { QWidget::showEvent(event); timer.start(); }
    void hideEvent(QHideEvent *event) override { timer.stop(); QWidget::hideEvent(event); }
    void paintEvent(QPaintEvent *) override {
        if (atlas.isNull() || frameCount < 1) return;
        const int frame = int(clock.elapsed() / interval) % frameCount;
        const QRect source((frame % columns) * frameWidth, (frame / columns) * frameHeight, frameWidth, frameHeight);
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(rect(), atlas, source);
    }
private:
    void queueBounds() {
        if (queued) return;
        queued = true;
        QTimer::singleShot(0, this, [this] { queued = false; updateBounds(); });
    }
    static QJsonObject manifest() {
        static const auto data = [] {
            QFile file(":/seasonal/manifest.json");
            if (!file.open(QIODevice::ReadOnly)) return QJsonObject{};
            return QJsonDocument::fromJson(file.readAll()).object();
        }();
        return data;
    }
    bool noticeFits(int width) const {
        if (!notice || notice->document()->isEmpty()) return true;
        if (width < 48) return false;
        QTextDocument document;
        document.setDefaultFont(notice->font());
        document.setDocumentMargin(notice->document()->documentMargin());
        document.setHtml(notice->toHtml());
        document.setTextWidth(qMax(1, width - 8));
        return document.size().height() <= notice->viewport()->height() + 1 &&
               document.idealWidth() <= width - 6;
    }
    void updateBounds() {
        if (!anchor || !bottom || !parentWidget()) return;
        const QString resolved = selection == "auto" ? themeForDate(QDate::currentDate()) : selection;
        const bool enabled = resolved != "none" && manifest().contains(resolved);
        const QPoint top = anchor->mapTo(parentWidget(), QPoint());
        const int baseline = bottom->mapTo(parentWidget(), QPoint(0, bottom->height())).y();
        const int height = qMin(64, baseline - top.y());
        // Both areas together are invariant under changes to the spacer width.
        const int left = notice ? notice->mapTo(parentWidget(), QPoint()).x() : top.x();
        const int right = parentWidget()->contentsRect().right() - 8;
        const int available = qMax(0, right - left + 1 - rightInset);
        QString chosen;
        int artWidth = 0;
        if (enabled && height >= 24) {
            for (const auto &candidate : {QStringLiteral("wide"), QStringLiteral("compact"), QStringLiteral("narrow")}) {
                const auto meta = manifest()[resolved].toObject()[candidate].toObject();
                const int sourceHeight = meta["height"].toInt(1);
                const int width = qRound(double(meta["width"].toInt()) * height / sourceHeight);
                if (width > 0 && width + 8 <= available && noticeFits(available - width - 8)) {
                    chosen = candidate; artWidth = width; break;
                }
            }
        }
        const int reserve = chosen.isEmpty() ? 0 : artWidth + rightInset + 8;
        if (anchor->width() != reserve) anchor->setFixedWidth(reserve);
        if (chosen.isEmpty()) {
            if (!enabled) { theme = resolved; variant.clear(); atlas = {}; }
            hide();
            return;
        }
        if (theme != resolved || variant != chosen || atlas.isNull()) {
            const auto meta = manifest()[resolved].toObject()[chosen].toObject();
            atlas.load(":/seasonal/" + meta["file"].toString());
            frameWidth = meta["width"].toInt(); frameHeight = meta["height"].toInt();
            columns = meta["columns"].toInt(1); frameCount = meta["frames"].toInt(1);
            interval = meta["interval"].toInt(100);
            theme = resolved; variant = chosen;
            timer.setInterval(interval);
        }
        setGeometry(right - rightInset - artWidth + 1, baseline - height, artWidth, height);
        setVisible(!atlas.isNull());
        if (isVisible()) timer.start();
        raise();
    }
    QPointer<QWidget> anchor, bottom;
    QPointer<QTextBrowser> notice;
    QTimer timer, calendarTimer;
    QElapsedTimer clock;
    QImage atlas;
    QString selection = "none", theme, variant;
    int frameWidth = 0, frameHeight = 0, columns = 1, frameCount = 0, interval = 100, rightInset = 0;
    bool queued = false;
};

inline SeasonalDecoration *installSeasonalDecoration(QWidget *central, QHBoxLayout *topBar,
                                                     QTabBar *tabs, QToolButton *filter) {
    auto *space = new SeasonalDecorationSpace(central);
    space->setObjectName("seasonalDecorationSpace");
    topBar->addWidget(space);
    auto *decoration = new SeasonalDecoration(central);
    decoration->setAnchors(space, tabs, filter->sizeHint().width(), central->findChild<QTextBrowser *>("data_view"));
    return decoration;
}
