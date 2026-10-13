#include "command.h"
#include "project.h"

namespace arena {

class RenameProjectCommand : public Command {
public:
    RenameProjectCommand(Project *p, QString from, QString to)
        : m_p(p), m_from(std::move(from)), m_to(std::move(to)) {}
    void execute() override { if (m_p) m_p->name = m_to; }
    void undo() override { if (m_p) m_p->name = m_from; }
    QString actionName() const override { return QStringLiteral("Rename project"); }
    int retainedBytes() const override { return 64 + m_from.size() + m_to.size(); }

private:
    Project *m_p = nullptr;
    QString m_from;
    QString m_to;
};

std::unique_ptr<Command> makeRenameProjectCommand(Project *p, const QString &to)
{
    return std::make_unique<RenameProjectCommand>(p, p ? p->name : QString(), to);
}

// --- UndoStack ---------------------------------------------------------------

UndoStack::UndoStack(int maxCommands, int maxBytes)
    : m_maxCommands(maxCommands), m_maxBytes(maxBytes) {}

int UndoStack::stackBytes(const QList<Command *> &list) const
{
    int bytes = 0;
    for (Command *c : list) bytes += c->retainedBytes();
    return bytes;
}

UndoStack::~UndoStack()
{
    for (Command *c : m_undo) delete c;
    for (Command *c : m_redo) delete c;
}

void UndoStack::evictToLimits()
{
    while (!m_undo.isEmpty() && m_undo.size() > m_maxCommands)
        delete m_undo.takeFirst();
    while (!m_undo.isEmpty() && stackBytes(m_undo) > m_maxBytes)
        delete m_undo.takeFirst();
}

void UndoStack::push(std::unique_ptr<Command> cmd)
{
    if (!cmd) return;
    cmd->execute();
    m_undo.append(cmd.release());
    // Starting a new action discards the redo branch.
    for (Command *c : m_redo) delete c;
    m_redo.clear();
    evictToLimits();
    if (onChanged) onChanged();
}

QString UndoStack::undoActionName() const
{
    return m_undo.isEmpty() ? QString() : m_undo.last()->actionName();
}
QString UndoStack::redoActionName() const
{
    return m_redo.isEmpty() ? QString() : m_redo.last()->actionName();
}

bool UndoStack::undo()
{
    if (m_undo.isEmpty()) return false;
    Command *cmd = m_undo.takeLast();
    cmd->undo();
    m_redo.append(cmd);
    if (onChanged) onChanged();
    return true;
}

bool UndoStack::redo()
{
    if (m_redo.isEmpty()) return false;
    Command *cmd = m_redo.takeLast();
    cmd->execute();
    m_undo.append(cmd);
    evictToLimits();
    if (onChanged) onChanged();
    return true;
}

void UndoStack::clear()
{
    for (Command *c : m_undo) delete c;
    for (Command *c : m_redo) delete c;
    m_undo.clear();
    m_redo.clear();
    if (onChanged) onChanged();
}

} // namespace arena
