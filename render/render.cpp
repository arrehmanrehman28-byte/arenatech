#include "render.h"

namespace arena {

QStringList EffectRegistry::effectTypes()
{
    return {
        QStringLiteral("transform"),        // position, scale, rotation, crop
        QStringLiteral("opacity"),
        QStringLiteral("blend"),
        QStringLiteral("kenburns"),
        QStringLiteral("color.basic"),      // exposure, contrast, saturation, temperature
        QStringLiteral("color.lut"),        // .cube
        QStringLiteral("blur"),
        QStringLiteral("sharpen"),
        QStringLiteral("speed"),            // with optional frame blending
        QStringLiteral("transition.cross_dissolve"),
        QStringLiteral("transition.wipe"),
        QStringLiteral("transition.push"),
        QStringLiteral("transition.fade_to_black"),
    };
}

bool EffectRegistry::isTransition(const QString &type)
{
    return type.startsWith(QLatin1String("transition."));
}

} // namespace arena
