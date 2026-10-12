#pragma once
// Small shared helpers (timecode formatting, process memory, byte formatting).
#include <QString>
#include <QLocale>

namespace arena {

// SMPTE-style timecode, HH:MM:SS:FF — kept identical in every language.
QString framesToTimecode(qint64 frames, double fps);
qint64 timecodeToFrames(const QString &tc, double fps);

// Current process resident memory in bytes (portable: VmRSS / GetProcessMemoryInfo).
qulonglong processResidentBytes();

// Locale-aware file size: "1.2 GB", "850 MB"… (uses QLocale for number format).
QString formatBytes(qulonglong bytes);

} // namespace arena
