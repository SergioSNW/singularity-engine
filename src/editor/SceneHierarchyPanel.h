#pragma once

#include "EditorPanel.h"

#include <string>
#include <vector>

struct SelectionState;
class Scene;
class CommandHistory;
struct Entity;

class SceneHierarchyPanel : public EditorPanel
{
public:
    SceneHierarchyPanel(SelectionState *selection, Scene *scene,
                        CommandHistory *history);
    void OnImGuiRender(float dt) override;

private:
    void DrawEntityNode(Entity &entity, int &to_delete_id);
    void DrawRenameRow();
    void DrawPrefabSaveModal();
    void DrawSpawnPrefabModal();
    void DuplicateNode(Entity &entity);
    // Undo-aware delete: captures the subtree, destroys it, and records the
    // inverse so Ctrl+Z re-spawns it. Leaves selection cleanup to the caller.
    void DeleteEntity(Entity &entity);

    // Spawn an entity carrying `mesh_path` (a mesh asset dropped from the
    // Content Browser), under `parent` (nullptr = scene root). Names it from
    // the file stem, selects it, and records the action for undo.
    void InstantiateMeshAsset(const std::string &mesh_path, Entity *parent);
    // Flush entity-creating drops queued while the tree was being drawn.
    void FlushPendingSpawns();

    SelectionState *m_selection;
    Scene *m_scene;
    CommandHistory *m_history;

    // Mesh drops onto an entity row must not CreateEntity while the tree
    // range-for over Scene::GetEntities() is live (a vector reallocation
    // would invalidate its iterators and crash). They are queued here and
    // flushed after the iteration completes.
    struct PendingMeshSpawn
    {
        std::string path;
        int parent_id;
    };
    std::vector<PendingMeshSpawn> m_pending_mesh_spawns;

    // Prefab authoring: save the selected entity (and its children) as a
    // reusable .prefab.json under assets/prefabs/.
    bool m_prefab_modal_open = false;
    char m_prefab_name[128] = {};
    int m_prefab_entity_id = -1;

    // Prefab instantiation: pick from the discovered prefab files.
    bool m_spawn_modal_open = false;
    std::vector<std::string> m_prefab_files;

    std::string m_status;  // last-action feedback line

    // Inline rename state (Phase 28, harmonized with the Content Browser):
    // right-clicking an entity -> "Rename..." opens a single-row InputText;
    // Enter or blur commits (undoable), Esc cancels.
    int m_rename_entity_id = -1;
    int m_rename_focus_id = -1;  // tracks the row that already grabbed focus
    char m_rename_buffer[128] = {};
};
