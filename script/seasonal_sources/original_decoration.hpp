#pragma once

#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QElapsedTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QPointer>
#include <QHBoxLayout>
#include <QTabBar>
#include <QToolButton>
#include <cmath>

class SeasonalDecorationSpace final : public QWidget {
public:
    explicit SeasonalDecorationSpace(QWidget *parent) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
        setMaximumWidth(230);
    }
    QSize sizeHint() const override { return {230, 0}; }
    QSize minimumSizeHint() const override { return {0, 0}; }
};

// Hand-drawn pixel artwork, independent of theme stylesheets and screen DPI.
// This widget never accepts input and only animates while visible.
class SeasonalDecoration final : public QWidget {
public:
    explicit SeasonalDecoration(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
        setMinimumSize(0, 0);
        setMaximumWidth(300);
        timer.setInterval(100);
        connect(&timer, &QTimer::timeout, this, [this] { update(); });
        clock.start();
    }
    QSize sizeHint() const override { return {230, 64}; }
    QSize minimumSizeHint() const override { return {0, 0}; }
    void setAnchors(QWidget *space, QWidget *divider, int filterWidth) {
        anchor = space; bottom = divider;
        rightInset = filterWidth + 6;
        space->installEventFilter(this); divider->installEventFilter(this);
        parentWidget()->installEventFilter(this);
        QTimer::singleShot(0, this, [this] { updateBounds(); });
    }
    void setNoticePriority(bool priority) {
        noticePriority = priority;
        updateBounds();
    }
    void setDecoration(const QString &value) {
        decoration = value;
        timer.setInterval(value == QStringLiteral("newyear") ? 50 : 100);
        enabled = value == QStringLiteral("halloween") || value == QStringLiteral("newyear");
        setVisible(enabled);
        if (anchor) anchor->setVisible(enabled);
        QTimer::singleShot(0, this, [this] { updateBounds(); });
        if (enabled && isVisible()) timer.start(); else timer.stop();
        update();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show)
            QTimer::singleShot(0, this, [this] { updateBounds(); });
        return false;
    }
    void showEvent(QShowEvent *event) override { QWidget::showEvent(event); if (enabled) timer.start(); }
    void hideEvent(QHideEvent *event) override { timer.stop(); QWidget::hideEvent(event); }
    void paintEvent(QPaintEvent *) override {
        if (!enabled || width() < 24 || height() < 24) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const qreal scale = qMin(height() / 46.0, width() / 136.0);
        p.translate(width() - 136 * scale, height() - 46 * scale);
        p.scale(scale, scale);
        p.setClipRect(0, 0, 136, 46);
        const double t = clock.elapsed() / 1000.0;
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        if (decoration == QStringLiteral("newyear")) { paintNewYear(p, t, dark); return; }
        const QColor ink = dark ? QColor("#372536") : QColor("#51394e");
        auto rect = [&](int x, int y, int w, int h, const QColor &c) { p.fillRect(x, y, w, h, c); };
        auto polygon = [&](std::initializer_list<QPoint> points, const QColor &c) {
            QPolygon poly; for (auto point : points) poly << point;
            p.setPen(Qt::NoPen); p.setBrush(c); p.drawPolygon(poly);
        };
        p.save(); p.translate(-20, 0);
        // A quiet web occupies the far corner, leaving the filter below untouched.
        p.setPen(QPen(dark ? QColor("#625769") : QColor("#a99dad"), .65));
        for (QPoint end : {QPoint(112, 0), QPoint(119, 17), QPoint(138, 24), QPoint(155, 27)}) p.drawLine(QPoint(155, 0), end);
        for (int r : {8, 16, 25, 34}) {
            QPolygon web; web << QPoint(155-r, 0) << QPoint(155-r+3, r/3) << QPoint(155-r/2, r/2) << QPoint(155-r/5, r*2/3) << QPoint(155, r*3/4);
            p.drawPolyline(web);
        }
        // The spider pauses at both ends instead of bouncing mechanically.
        const int spiderY = 16 + qRound(7 * (1 - std::cos(t * .7)));
        p.setPen(QPen(dark ? QColor("#9a899e") : QColor("#77627f"), .7));
        p.drawLine(140, 6, 140, spiderY);
        const int leg = int(t * 2) % 2;
        const QColor spiderInk = dark ? QColor("#937a9e") : ink;
        p.setPen(QPen(spiderInk, 1));
        for (int side : {-1, 1}) for (int y : {-1, 1, 3}) {
            p.drawLine(140 + side*2, spiderY+y, 140+side*5, spiderY+y-2+leg);
            p.drawLine(140+side*5, spiderY+y-2+leg, 140+side*6, spiderY+y+1);
        }
        rect(138, spiderY-1, 5, 5, spiderInk); rect(139, spiderY-2, 3, 7, spiderInk);
        rect(139, spiderY, 1, 1, "#ded7e7"); rect(141, spiderY, 1, 1, "#ded7e7");
        p.restore();
        // A tiny ghost peeks from alternate edges, fully clipped inside this widget.
        const double cycle = std::fmod(t, 18.0);
        const bool above = cycle < 9;
        const double phase = std::fmod(cycle, 9.0);
        const double peek = phase < 5 ? std::pow(std::sin(phase * 3.141592653589793 / 5), 2) : 0;
        const int ghostY = above ? qRound(-18 + peek*23) : qRound(46 - peek*21);
        p.save(); p.translate(85, ghostY);
        polygon({{4,0},{11,0},{15,3},{16,8},{16,18},{13,16},{10,18},{7,16},{4,18},{0,16},{0,7},{1,3}}, ink);
        polygon({{4,1},{10,1},{14,4},{15,8},{15,15},{12,14},{10,16},{7,14},{4,16},{1,14},{1,7},{2,4}}, "#d9e5e7");
        rect(2,6,3,7,"#a3cbd1"); rect(5,5,3,4,"#203349"); rect(10,5,3,4,"#203349");
        rect(6,5,1,1,"#ffffff"); rect(11,5,1,1,"#ffffff"); rect(8,10,2,2,"#486c7b");
        p.restore();
        // Uneven overlapping leaf piles settle around the sides of the pumpkins.
        auto leaf = [&](int x, int y, double angle, double scale, const char *color) {
            p.save(); p.translate(x, y); p.rotate(angle); p.scale(scale, scale);
            polygon({{0,0},{3,-1},{2,-4},{5,-3},{6,-6},{8,-3},{11,-4},{10,-1},{13,0},{9,3},{6,4},{2,3}}, color);
            rect(5, -2, 1, 6, "#e6a05c"); p.restore();
        };
        leaf(3, 43, -28, .72, "#955132");
        leaf(10, 39, 22, .80, "#b76736");
        leaf(14, 45, -12, .86, "#cd8240");
        leaf(23, 42, -52, .66, "#a75c31");
        leaf(81, 43, -20, .70, "#ad6234");
        leaf(115, 40, 25, .76, "#955132");
        leaf(110, 45, -16, .88, "#cd8240");
        leaf(124, 44, -48, .62, "#b76736");
        // Three pumpkins share a restrained orange palette and have independent expressions.
        auto pumpkin = [&](int x, int y, double s, int variant) {
            p.save(); p.translate(x,y); p.scale(s,s);
            polygon({{5,0},{10,-2},{16,0},{21,-1},{26,2},{29,7},{29,17},{25,22},{5,22},{1,18},{0,8},{2,3}}, ink);
            polygon({{6,1},{11,0},{16,2},{21,1},{25,3},{27,8},{27,16},{23,20},{6,20},{3,16},{2,8},{4,4}}, "#d9772c");
            rect(8,2,4,17,"#ec983e"); rect(19,3,3,16,"#be5b26"); rect(5,5,2,10,"#f2ac51");
            polygon({{12,1},{12,-5},{15,-7},{19,-7},{19,-5},{16,-5},{15,1}}, "#66704a");
            rect(14,-5,1,4,"#92a165");
            const bool blink = std::fmod(t + variant * 1.7, 7.0) < .22;
            if (blink) { rect(6,8,6,2,ink); rect(18,8,6,2,ink); }
            else {
                polygon({{6,6},{12,9},{11,12},{6,11}}, ink);
                polygon({{23,6},{18,9},{19,12},{24,11}}, ink);
                rect(8,9,3,1,"#ffe89a"); rect(20,9,2,1,"#ffe89a");
            }
            polygon({{14,11},{12,14},{16,14}}, ink);
            const bool grin = int(t / 4 + variant) % 3 != 0;
            if (grin) {
                polygon({{5,14},{9,16},{12,15},{15,17},{18,15},{22,16},{25,14},{23,19},{7,19}}, ink);
                rect(9,16,2,1,"#ffe89a"); rect(19,16,2,1,"#ffe89a");
            } else { rect(10,16,10,3,ink); rect(13,16,2,1,"#ffe89a"); }
            p.restore();
        };
        pumpkin(20, 33, .58, 0); pumpkin(33, 21, 1.12, 1); pumpkin(60, 32, .64, 2);
        // Candle flame flickers by one pixel, without flashes or bloom.
        p.save(); p.translate(-14, 0);
        const int flicker = int(t*5) % 3;
        rect(111,34,7,11,ink); rect(112,34,5,10,"#e8d3a3"); rect(112,35,2,8,"#fff0c4");
        rect(115,35,1,4,"#c5a777"); rect(111,44,8,2,"#725348"); rect(114,31,1,3,ink);
        polygon({{114,24-flicker},{117,29},{116,32},{112,32},{111,29}}, "#dc8738");
        polygon({{114,27},{116,30},{114,32},{113,30}}, "#ffe6a0");
        p.restore();
        // Small foreground leaves touch only the lower edges, leaving faces clear.
        leaf(76, 45, -14, .54, "#b76736");
        leaf(91, 45, 22, .52, "#cd8240");
    }
private:
    static void paintNewYear(QPainter &p, double t, bool dark) {
        const QColor outline(dark ? "#385361" : "#607c88");
        const QColor snow("#e5f2f3"), shade("#a8ccd5");
        auto rect = [&](int x,int y,int w,int h,const QColor &c) { p.fillRect(x,y,w,h,c); };
        auto poly = [&](std::initializer_list<QPoint> pts,const QColor &c) {
            QPolygon a; for (auto v:pts) a << v; p.setPen(Qt::NoPen); p.setBrush(c); p.drawPolygon(a);
        };
        // Low snow banks keep the entire scene seated on the divider.
        poly({{4,44},{10,41},{22,41},{27,39},{40,41},{56,40},{68,42},{88,40},{102,42},{116,41},{132,44},{132,46},{4,46}},outline);
        poly({{5,44},{12,42},{24,42},{28,40},{40,42},{57,41},{69,43},{88,41},{103,43},{118,42},{130,44},{130,46},{5,46}},snow);
        rect(36,34,7,10,"#70503a");
        poly({{39,5},{49,17},{45,17},{56,29},{50,29},{62,40},{17,40},{28,29},{23,29},{34,17},{30,17}},outline);
        poly({{39,8},{46,17},{42,17},{52,28},{46,28},{58,38},{22,38},{32,28},{27,28},{37,17},{33,17}},"#34715d");
        poly({{39,8},{39,17},{33,17},{37,17},{27,28},{32,28},{22,38},{32,38},{40,27},{37,27},{43,17}},"#489778");
        poly({{39,1},{41,4},{45,4},{42,7},{43,11},{39,9},{35,11},{36,7},{33,4},{37,4}},"#e9bf68");
        rect(31,18,8,2,snow);rect(26,29,9,2,snow);rect(45,29,7,2,snow);
        for (int i=0;i<7;++i) {
            const QPoint lights[]={{37,15},{43,20},{34,24},{46,27},{29,34},{39,34},{51,35}};
            const QColor colors[]={QColor("#e88672"),QColor("#edc977"),QColor("#a0c4df")};
            const bool glow=(int(t*1.5)+i)%4==0;
            rect(lights[i].x(),lights[i].y(),2,2,glow?QColor("#fff0b4"):colors[i%3]);
        }
        // The snowman blinks; its twig arms remain still.
        poly({{75,26},{88,26},{94,31},{95,39},{91,43},{73,43},{68,39},{69,31}},outline);
        poly({{76,27},{87,27},{92,32},{93,38},{89,41},{74,41},{70,38},{71,32}},snow);
        poly({{77,14},{87,14},{91,18},{91,25},{87,29},{76,28},{73,24},{73,18}},outline);
        poly({{78,15},{86,15},{89,18},{89,24},{86,27},{77,26},{75,23},{75,19}},snow);
        rect(72,32,2,6,shade);rect(76,19,1,4,shade);
        rect(77,8,11,7,"#344553");rect(74,14,17,2,"#344553");rect(77,12,11,2,"#b75e57");
        const bool blink=std::fmod(t,6.0)<.22;
        rect(78,20,2,blink?1:2,"#344553");rect(85,20,2,blink?1:2,"#344553");
        poly({{81,23},{88,24},{81,25}},"#d88d48");rect(80,27,4,1,"#607c88");
        rect(74,28,17,3,"#b75e57");rect(86,30,4,7,"#cf7e67");
        rect(80,34,2,2,"#526875");rect(80,39,2,2,"#526875");
        p.setPen(QPen(QColor("#977359"),1));
        p.drawLine(70,32,64,28);p.drawLine(64,28,63,25);
        // Fixed twig arms drawn on an integer pixel grid.
        const int hand=27;
        auto twig=[&](int x0,int y0,int x1,int y1) {
            const int count=qMax(std::abs(x1-x0),std::abs(y1-y0));
            for(int i=0;i<=count;++i) {
                const double f=count ? double(i)/count : 0;
                rect(qRound(x0+(x1-x0)*f),qRound(y0+(y1-y0)*f),1,1,"#977359");
            }
        };
        twig(70,32,64,28);twig(64,28,63,25);
        twig(93,32,100,hand);twig(100,hand,103,hand-2);twig(100,hand,100,hand-4);
        rect(105,34,13,10,"#ad5e62");rect(110,34,2,10,"#edc977");rect(104,33,15,2,"#c97b77");
        poly({{111,33},{106,30},{105,31},{106,33},{111,33},{115,30},{117,31},{116,33}},"#edc977");
        // Sparse flakes drift gently, without flashes or a full-window overlay.
        for(int i=0;i<17;++i) {
            // Independent phases, periods and gusts avoid rows of evenly spaced flakes.
            const qreal seed=std::fmod((i+1)*.61803398875,1.0);
            const qreal speed=1.3+std::fmod((i+1)*.754877666,1.0)*1.7;
            const qreal x=5+seed*124+2.4*std::sin(t*(.19+seed*.24)+i*2.41)
                         +1.2*std::sin(t*.73+i*1.37);
            const qreal y=std::fmod(t*speed+std::fmod((i+1)*.41421356237,1.0)*47,47.0)-3;
            QColor c=dark?QColor("#afcbd5"):QColor("#8aabb9");
            p.fillRect(QRectF(x,y,1,1),c);
            if(i%5==0) { p.fillRect(QRectF(x-1,y,3,1),c);p.fillRect(QRectF(x,y-1,1,3),c); }
        }
    }
    void updateBounds() {
        if (!anchor || !bottom || !parentWidget()) return;
        // Give status text the available width before reserving room for artwork.
        const int available = noticePriority ? 0 : 230;
        anchor->setMinimumWidth(enabled && !noticePriority ? 90 : 0);
        if (anchor->maximumWidth() != available) anchor->setMaximumWidth(available);
        anchor->setVisible(enabled && !noticePriority);
        setVisible(enabled && !noticePriority);

        const QPoint top = anchor->mapTo(parentWidget(), QPoint(0, 0));
        const int baseline = bottom->mapTo(parentWidget(), QPoint(0, bottom->height())).y();
        const int artWidth = qMin(190, qMax(0, anchor->width() - rightInset));
        const int artHeight = qMin(64, baseline - top.y());
        const bool fits = enabled && !noticePriority && available > 0 && artWidth >= 24 && artHeight >= 24;
        setGeometry(top.x() + qMax(0, anchor->width() - rightInset - artWidth), baseline - artHeight, artWidth, artHeight);
        setVisible(fits);
        if (fits) timer.start(); else timer.stop();
        raise();
    }
    QPointer<QWidget> anchor, bottom;
    int rightInset = 0;
    QString decoration;
    bool enabled = false;
    bool noticePriority = false;
    QTimer timer;
    QElapsedTimer clock;
};

inline SeasonalDecoration *installSeasonalDecoration(QWidget *central, QHBoxLayout *topBar,
                                                     QTabBar *tabs, QToolButton *filter) {
    auto *space = new SeasonalDecorationSpace(central);
    space->setObjectName("seasonalDecorationSpace");
    topBar->addWidget(space);
    auto *decoration = new SeasonalDecoration(central);
    decoration->setAnchors(space, tabs, filter->sizeHint().width());
    return decoration;
}
