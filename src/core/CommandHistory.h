#pragma once

#include <memory>
#include <string>
#include <vector>

class Scene;
class Entity;

// Command pattern base for the editor's undo/redo history (Phase 22).
//
// A Command owns the two sides of one reversible editor action: Execute() is
// "apply / redo" and Undo() is "revert". Concrete commands are defined in
// CommandHistory.cpp; the public surface only needs the abstract base plus the
// stack that drives them.
class Command
{
public:
    virtual ~Command() = default;
    virtual void Execute() = 0;   // apply (and re-apply on redo)
    virtual void Undo() = 0;      // revert

    const std::string &Description() const { return m_description; }

protected:
    std::string m_description;
};

// Global undo/redo transaction stack (Ctrl+Z / Ctrl+Y) shared by every editor
// panel that mutates the scene.
//
// Two ways in:
//
//   * Execute(cmd)        — run the command now (e.g. "Delete Entity") and
//                           remember it for Undo.
//   * Push(cmd)           — register an operation that already happened
//                           (e.g. an entity was just created) so the first
//                           Undo reverts it.
//
// Property edits and gizmo drags are wrapped in a transaction: BeginEntityEdit
// snapshots the entity's entire state, and EndEntityEdit pushes a single undo
// command capturing before/after. Snapshotting the whole entity (instead of one
// field) means one generic command covers tag renames, transform drags, and
// every component property edit in the Inspector.
class CommandHistory
{
public:
    explicit CommandHistory(Scene *scene);

    void Execute(std::unique_ptr<Command> cmd);
    void Push(std::unique_ptr<Command> cmd);
    void Undo();
    void Redo();
    void Clear();

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    const Command *PeekUndo() const { return m_undo.empty() ? nullptr : m_undo.back().get(); }
    const Command *PeekRedo() const { return m_redo.empty() ? nullptr : m_redo.back().get(); }
    // Commands ordered oldest (0) → newest (UndoSize()-1, next to undo).
    const Command *UndoAt(size_t i) const { return i < m_undo.size() ? m_undo[i].get() : nullptr; }
    const Command *RedoAt(size_t i) const { return i < m_redo.size() ? m_redo[i].get() : nullptr; }
    size_t UndoSize() const { return m_undo.size(); }
    size_t RedoSize() const { return m_redo.size(); }

    // Property-edit transaction over one entity. EndEntityEdit() is a no-op
    // unless BeginEntityEdit() was called and the entity state actually
    // changed, so a click that selects but never edits costs nothing.
    void BeginEntityEdit(int entity_id, const char *description);
    void EndEntityEdit();

    // Register an already-created entity (duplicate, new asset spawn) as an
    // undoable action: the first Undo destroys `entity` (and its subtree), a
    // later Redo re-spawns it from a serialized capture. `entity` must still
    // be alive when this is called.
    void PushSpawn(Entity &entity, const char *description);

    // Delete `entity` (and its subtree) as an undoable action: Undo re-spawns
    // the captured subtree, Redo removes it again.
    void ExecuteDelete(Entity &entity, const char *description);

    Scene *GetScene() const { return m_scene; }

private:
    Scene *m_scene;
    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
    size_t m_limit;

    // Active transaction state.
    int m_edit_entity;
    std::string m_edit_desc;
    class EntitySnapshot *m_edit_before;
};
