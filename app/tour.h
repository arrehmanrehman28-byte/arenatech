#pragma once
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QWidget>

// M8 first-run tour: a compact card pinned to the main window's bottom-right
// that walks through the primary areas (6 steps, ~10 s each = ~60 s total).
// - "Skip tour" at any time; Help > Tour replays it on demand.
// - Slides in subtly (position tween) UNLESS Settings::reduceMotion() is set
//   (D-040: the reduce-motion setting must actually disable motion).
// - Each step highlights its target widget via the "tourTarget" property,
//   styled by the themer's accent ring (visible focus/affordance, D-034).
namespace arena {

class Tour : public QWidget {
    Q_OBJECT
public:
    struct Step {
        QString titleKey; // i18n key
        QString bodyKey;  // i18n key
        QString targetObjectName; // main-window objectName to highlight ("" = none)
    };
    static QList<Step> defaultSteps();

    explicit Tour(QWidget *parent);
    void start(); // show at step 0
    int index() const { return m_idx; }
    int count() const { return m_steps.size(); }
    bool atLast() const { return m_idx == m_steps.size() - 1; }
    bool isRunning() const { return isVisible(); }
    QString targetObjectName() const;

    void next(); // advance, or finish on the last step
    void back();
    void skip(); // finish without completing

signals:
    void finished(bool completed);

private:
    void loadStep();
    void finish(bool completed);

    QList<Step> m_steps;
    int m_idx = 0;
    QLabel *m_title;
    QLabel *m_body;
    QLabel *m_count;
    QPushButton *m_back;
    QPushButton *m_next;
    QPushButton *m_skip;
    QPropertyAnimation *m_tween = nullptr;
};

} // namespace arena
