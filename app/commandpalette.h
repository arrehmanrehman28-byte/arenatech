#pragma once
// Command palette (Ctrl+K): fuzzy search over every command, setting, and AI
// action, with the shortcut shown next to each result. Prefixing the query
// with "/ai" (or pressing Tab) switches to natural-language mode.
#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QList>
#include <functional>

namespace arena {

struct PaletteCommand {
    QString nameKey;       // i18n key for the name
    QString shortcut;      // display shortcut (or empty)
    std::function<void()> action;
};

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget *parent = nullptr);
    void setCommands(const QList<PaletteCommand> &commands);
    bool aiMode() const { return m_aiMode; }
    QString aiText() const;

signals:
    void aiRequested(const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void refill();
    QLineEdit *m_search;
    QListView *m_list;
    QStandardItemModel *m_model;
    QList<PaletteCommand> m_commands;
    bool m_aiMode = false;
};

} // namespace arena
