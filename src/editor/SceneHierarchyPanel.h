#pragma once

#include "EditorPanel.h"

#include <string>
#include <vector>

struct SelectionState;
class Scene;
struct Entity;

class SceneHierarchyPanel : public EditorPanel
{
public:
    SceneHierarchyPanel(SelectionState *selection, Scene *scene);
    void OnImGuiRender(float dt) override;

private:
    void DrawEntityNode(Entity &entity, int &to_delete_id);
    void DrawPrefabSaveModal();
    void DrawSpawnPrefabModal();
    void DuplicateNode(Entity &entity);

    SelectionState *m_selection;
    Scene *m_scene;

    // Prefab authoring: save the selected entity (and its children) as a
    // reusable .prefab.json under assets/prefabs/.
    bool m_prefab_modal_open = false;
    char m_prefab_name[128] = {};
    int m_prefab_entity_id = -1;

    // Prefab instantiation: pick from the discovered prefab files.
    bool m_spawn_modal_open = false;
    std::vector<std::string> m_prefab_files;

    std::string m_status;  // last-action feedback line
};
