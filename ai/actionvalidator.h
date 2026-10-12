#pragma once
// Strict schema validation for natural-language command output.
// The model must emit a JSON array of action objects; anything that fails
// validation is shown to the user as rejected — never executed.
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace arena {
namespace actionValidator {

struct ValidationResult {
    bool ok = false;
    QStringList errors;
};

QStringList supportedOps();
QStringList transitions();
QStringList exportPresets();

// Validates the whole action list.
ValidationResult validateActions(const QJsonArray &actions);

// Extracts the first JSON array found in raw model text (models sometimes
// wrap JSON in prose or code fences). Returns an empty array when none.
QJsonArray extractJsonArray(const QString &rawModelText);

} // namespace actionValidator
} // namespace arena
