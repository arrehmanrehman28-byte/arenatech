#pragma once
// Command pattern for undo/redo. Every UI action and every accepted AI action
// is a Command; the stack is memory-capped and reports human-readable names
// (shown on the Undo/Redo buttons, e.g. "Undo: Trim clip end").
#include <QString>
#include <QList>
#include <QPointer>
#include <memory>
#include <functional>

namespace arena {

class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString actionName() const = 0; // used for "Undo: {actionName}"
    // Approximate retained payload size, for the stack's memory cap.
    virtual int retainedBytes() const { return 64; }
};

class UndoStack {
public:
    // maxCommands: hard cap; maxBytes: soft memory cap (oldest entries evicted).
    explicit UndoStack(int maxCommands = 100, int maxBytes = 4 * 1024 * 1024);
    ~UndoStack();

    void push(std::unique_ptr<Command> cmd); // executes the command, then pushes
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int undoDepth() const { return m_undo.size(); }
    int redoDepth() const { return m_redo.size(); }
    QString undoActionName() const; // "" if empty
    QString redoActionName() const;

    bool undo();
    bool redo();
    void clear();

private:
    // Raw pointers, manually owned: QList cannot hold move-only types.
    void evictToLimits();
    int stackBytes(const QList<Command *> &list) const;
    QList<Command *> m_undo;
    QList<Command *> m_redo;
    int m_maxCommands;
    int m_maxBytes;
};

struct Project;

// Factory kept out-of-line so the header stays free of Project includes.
std::unique_ptr<Command> makeRenameProjectCommand(Project *p, const QString &to);

} // namespace arena
