#include "tour.h"
#include "i18n.h"
#include "settings.h"

#include <QAbstractAnimation>
#include <QHBoxLayout>
#include <QStyle>
#include <QVBoxLayout>

namespace arena {

QList<Tour::Step> Tour::defaultSteps()
{
    // Six steps, ~10 s of reading each => ~60 s total (M8 scope).
    // Targets are main-window objectNames (see MainWindow::buildUi).
    return {
        {QStringLiteral("tour.title.0"), QStringLiteral("tour.body.0"),
         QStringLiteral("MediaBinDock")},
        {QStringLiteral("tour.title.1"), QStringLiteral("tour.body.1"),
         QStringLiteral("TimelineWidget")},
        {QStringLiteral("tour.title.2"), QStringLiteral("tour.body.2"),
         QStringLiteral("MonitorBox")},
        {QStringLiteral("tour.title.3"), QStringLiteral("tour.body.3"),
         QStringLiteral("SubtitlesDock")},
        {QStringLiteral("tour.title.4"), QStringLiteral("tour.body.4"),
         QStringLiteral("InspectorDock")},
        {QStringLiteral("tour.title.5"), QStringLiteral("tour.body.5"),
         QStringLiteral("AiDock")},
    };
}

Tour::Tour(QWidget *parent)
    : QWidget(parent)
    , m_steps(defaultSteps())
{
    setObjectName(QStringLiteral("TourCard"));

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("TourTitle"));
    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("TourBody"));
    m_body->setWordWrap(true);
    m_body->setMinimumWidth(340);
    m_body->setMaximumWidth(380);
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("TourCount"));
    m_back = new QPushButton(this);
    m_next = new QPushButton(this);
    m_skip = new QPushButton(this);

    auto *top = new QHBoxLayout;
    top->addWidget(m_back, 0);
    top->addStretch(1);
    top->addWidget(m_count, 0);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_skip, 0);
    buttons->addStretch(1);
    buttons->addWidget(m_next, 0);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(8);
    lay->addWidget(m_title);
    lay->addWidget(m_body);
    lay->addLayout(top);
    lay->addLayout(buttons);

    connect(m_next, &QPushButton::clicked, this, &Tour::next);
    connect(m_back, &QPushButton::clicked, this, &Tour::back);
    connect(m_skip, &QPushButton::clicked, this, &Tour::skip);
}

void Tour::start()
{
    m_idx = 0;
    loadStep(); // set texts + highlight target 0
    show();
    raise();
    adjustSize(); // real sizeHint now that the card is visible

    const int finalX = qMax(8, parentWidget()->width() - width() - 24);
    const int finalY = qMax(8, parentWidget()->height() - height() - 40);
    const bool motionOff = Settings::instance().reduceMotion();
    if (motionOff) {
        // Reduce motion (D-040): appear instantly at the final spot.
        if (m_tween) {
            m_tween->stop();
            delete m_tween;
            m_tween = nullptr;
        }
        setGeometry(finalX, finalY, width(), height());
    } else {
        // Subtle 180 ms slide-in from the right (theme "motion.slowMs" intent).
        setGeometry(finalX + 24, finalY, width(), height());
        if (!m_tween)
            m_tween = new QPropertyAnimation(this, "pos", this);
        m_tween->stop();
        m_tween->setDuration(180);
        m_tween->setStartValue(pos());
        m_tween->setEndValue(QPoint(finalX, finalY));
        m_tween->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

QString Tour::targetObjectName() const
{
    return m_idx < m_steps.size() ? m_steps.at(m_idx).targetObjectName : QString();
}

void Tour::loadStep()
{
    const Step &s = m_steps.at(m_idx);
    m_title->setText(arenaTr(s.titleKey));
    m_body->setText(arenaTr(s.bodyKey));
    m_count->setText(
        arenaTr("tour.stepOf", {{"n", QString::number(m_idx + 1)},
                                {"total", QString::number(m_steps.size())}}));
    m_back->setEnabled(m_idx > 0);
    m_back->setText(arenaTr("tour.back"));
    m_next->setText(arenaTr(atLast() ? "tour.done" : "tour.next"));
    m_skip->setText(arenaTr("tour.skip"));

    // Highlight the target widget (themer styles [tourTarget="true"]).
    if (auto *p = parentWidget()) {
        if (m_idx > 0 && m_idx - 1 < m_steps.size()
            && !m_steps.at(m_idx - 1).targetObjectName.isEmpty())
            if (QWidget *old = p->findChild<QWidget *>(m_steps.at(m_idx - 1).targetObjectName)) {
                old->setProperty("tourTarget", false);
                style()->unpolish(old);
                style()->polish(old);
            }
        if (!s.targetObjectName.isEmpty())
            if (QWidget *w = p->findChild<QWidget *>(s.targetObjectName)) {
                w->setProperty("tourTarget", true);
                style()->unpolish(w);
                style()->polish(w);
            }
    }
}

void Tour::next()
{
    if (atLast()) {
        finish(true);
        return;
    }
    ++m_idx;
    loadStep();
}

void Tour::back()
{
    if (m_idx == 0)
        return;
    --m_idx;
    loadStep();
}

void Tour::skip()
{
    finish(false);
}

void Tour::finish(bool completed)
{
    if (m_tween) {
        m_tween->stop();
        delete m_tween;
        m_tween = nullptr;
    }
    // Remove the highlight from the current target.
    if (auto *p = parentWidget())
        if (!targetObjectName().isEmpty())
            if (QWidget *w = p->findChild<QWidget *>(targetObjectName())) {
                w->setProperty("tourTarget", false);
                style()->unpolish(w);
                style()->polish(w);
            }
    hide();
    emit finished(completed);
}

} // namespace arena
