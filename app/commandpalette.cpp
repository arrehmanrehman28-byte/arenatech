#include "commandpalette.h"
#include "i18n.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <algorithm>

namespace arena {

CommandPalette::CommandPalette(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(arenaTr("palette.placeholder"));
    setModal(true);
    setMinimumWidth(560);
    setMaximumWidth(720);
    setWindowFlags(windowFlags() | Qt::Popup);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(arenaTr("palette.placeholder"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    lay->addWidget(m_search);

    m_list = new QListView;
    m_model = new QStandardItemModel(this);
    m_list->setModel(m_model);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(m_list, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(m_list, &QListView::activated, this, [this](const QModelIndex &idx) {
        const QString id = m_model->data(idx, Qt::UserRole).toString();
        if (id == QLatin1String("ai")) {
            emit aiRequested(m_aiMode ? m_search->text() : QString());
            accept();
            return;
        }
        int n = 0;
        for (const auto &c : m_commands) {
            if (id == QString::number(n)) { c.action(); break; }
            ++n;
        }
        accept();
    });
}

void CommandPalette::setCommands(const QList<PaletteCommand> &commands)
{
    m_commands = commands;
    refill();
}

QString CommandPalette::aiText() const
{
    QString t = m_search->text();
    if (t.startsWith(QLatin1String("/ai"))) t.remove(0, 3);
    return t.trimmed();
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_search && ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Tab) {
            m_aiMode = !m_aiMode;
            refill();
            return true;
        }
    }
    return QDialog::eventFilter(obj, ev);
}

// Fuzzy: subsequence match; exact-prefix and substring matches rank higher.
static int score(const QString &query, const QString &target)
{
    if (query.isEmpty()) return 1;
    const QString q = query.toLower();
    const QString t = target.toLower();
    if (t.startsWith(q)) return 1000 - t.size() / 10;
    if (t.contains(q)) return 500 - t.indexOf(q);
    int qi = 0;
    int s = 0;
    for (int ti = 0; ti < t.size() && qi < q.size(); ++ti) {
        if (t.at(ti) == q.at(qi)) { ++qi; ++s; }
    }
    return qi == q.size() ? s : -1;
}

void CommandPalette::refill()
{
    m_model->clear();
    const QString q = m_search->text();
    m_aiMode = q.trimmed().startsWith(QLatin1String("/ai"));

    if (m_aiMode) {
        auto *item = new QStandardItem(arenaTr("palette.aiMode"));
        item->setData(QStringLiteral("ai"), Qt::UserRole);
        auto *shortcut = new QStandardItem(QStringLiteral("/ai"));
        m_model->appendRow(QList<QStandardItem *>{item, shortcut});
        m_search->setPlaceholderText(arenaTr("palette.aiMode"));
        return;
    }
    m_search->setPlaceholderText(arenaTr("palette.placeholder"));

    struct Row { int s; int idx; const PaletteCommand *c; };
    QList<Row> rows;
    for (int i = 0; i < m_commands.size(); ++i) {
        const int s = score(q, arenaTr(m_commands.at(i).nameKey));
        if (s > 0) rows.append({s, i, &m_commands.at(i)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.s != b.s ? a.s > b.s : a.idx < b.idx;
    });
    for (const Row &r : rows) {
        auto *name = new QStandardItem(arenaTr(r.c->nameKey));
        name->setData(QString::number(r.idx), Qt::UserRole);
        auto *shortcut = new QStandardItem(r.c->shortcut);
        m_model->appendRow(QList<QStandardItem *>{name, shortcut});
    }
    if (rows.isEmpty()) {
        auto *none = new QStandardItem(arenaTr("palette.noResults"));
        auto *emptyShortcut = new QStandardItem;
        m_model->appendRow(QList<QStandardItem *>{none, emptyShortcut});
    }
    if (m_model->rowCount() > 0) m_list->setCurrentIndex(m_model->index(0, 0));
}

} // namespace arena
