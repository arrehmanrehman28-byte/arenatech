#include "media.h"
#include <QFileInfo>

namespace arena {

MediaType MediaProbe::typeFromPath(const QString &path)
{
    const QString e = QFileInfo(path).suffix().toLower();
    if (QStringList{QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
                    QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("avi"),
                    QStringLiteral("mxf"), QStringLiteral("m2ts")}
            .contains(e))
        return MediaType::Video;
    if (QStringList{QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("aac"),
                    QStringLiteral("flac"), QStringLiteral("opus"), QStringLiteral("m4a"),
                    QStringLiteral("ogg"), QStringLiteral("wma")}
            .contains(e))
        return MediaType::Audio;
    if (QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                    QStringLiteral("webp"), QStringLiteral("tif"), QStringLiteral("bmp")}
            .contains(e))
        return MediaType::Image;
    return MediaType::Unknown;
}

MediaInfo MediaProbe::probe(const QString &path)
{
    MediaInfo info;
    info.path = path;
    QFileInfo fi(path);
    info.exists = fi.exists() && fi.isFile();
    if (info.exists) info.sizeBytes = fi.size();
    info.type = typeFromPath(path);
    return info;
}

} // namespace arena
