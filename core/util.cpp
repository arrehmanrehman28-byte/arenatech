#include "util.h"
#include <QTextStream>
#include <QFile>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace arena {

static int fpsInt(double fps) { return qMax(1, int(fps + 0.5)); }

QString framesToTimecode(qint64 frames, double fps)
{
    if (frames < 0) frames = 0;
    const int f = fpsInt(fps);
    const qint64 totalFrames = frames;
    const int ff = int(totalFrames % f);
    const qint64 totalSecs = totalFrames / f;
    const int ss = int(totalSecs % 60);
    const int mm = int((totalSecs / 60) % 60);
    const int hh = int(totalSecs / 3600);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0'))
        .arg(ss, 2, 10, QChar('0')).arg(ff, 2, 10, QChar('0'));
}

qint64 timecodeToFrames(const QString &tc, double fps)
{
    const QStringList p = tc.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (p.size() != 4) return 0;
    bool ok = false;
    const int h = p[0].toInt(&ok); if (!ok) return 0;
    const int m = p[1].toInt();
    const int s = p[2].toInt();
    const int f = p[3].toInt();
    return ((qint64(h) * 3600) + qint64(m) * 60 + s) * fpsInt(fps) + f;
}

qulonglong processResidentBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
        return pmc.WorkingSetSize;
    return 0;
#else
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    const QByteArray all = f.readAll();
    const QList<QByteArray> lines = all.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("VmRSS:")) {
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() >= 2) return qulonglong(parts[1].toLongLong()) * 1024ULL;
        }
    }
    return 0;
#endif
}

QString formatBytes(qulonglong bytes)
{
    const double kb = 1024.0, mb = kb * 1024, gb = mb * 1024;
    QLocale loc;
    if (bytes >= gb) return loc.toString(bytes / gb, 'f', 1) + QStringLiteral(" GB");
    if (bytes >= mb) return loc.toString(bytes / mb, 'f', 1) + QStringLiteral(" MB");
    if (bytes >= kb) return loc.toString(bytes / kb, 'f', 0) + QStringLiteral(" KB");
    return loc.toString((double)bytes) + QStringLiteral(" B");
}

} // namespace arena
