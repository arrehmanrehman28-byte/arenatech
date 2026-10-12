#pragma once
// Compositor + effect registry. The registry defines the effect contract
// used by the Inspector (M3); the Compositor is the single place where a
// timeline frame becomes pixels (never on the UI thread).
#include <QJsonObject>
#include <QString>
#include <QStringList>

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

class Compositor {
public:
    struct FrameRequest {
        qint64 frame = 0;
        double fps = 30.0;
        int width = 1920;
        int height = 1080;
    };
    // M3: D3D11 (preferred) / OpenGL compositing with an LRU frame cache
    // (default 512 MB, configurable). Returns a handle, not a pixel copy.
    static bool isAvailable() { return false; }
};

} // namespace arena
