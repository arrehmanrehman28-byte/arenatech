#pragma once
// Compositor + effect registry. The registry defines the effect contract
// used by the Inspector (M3); the Compositor is the single place where a
// timeline frame becomes pixels (never on the UI thread).
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "compositor.h" // the compositor lives here (M3)

namespace arena {

struct Effect {
    QString type;     // e.g. "transform", "color.basic", "transition.cross_dissolve"
    QJsonObject params;
};

class EffectRegistry {
public:
    // Minimum effect set from the spec (M3 implements these).
    static QStringList effectTypes();
    static bool isTransition(const QString &type);
};

} // namespace arena
