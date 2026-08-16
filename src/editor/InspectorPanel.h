#pragma once

#include "EditorPanel.h"

#include <functional>

struct SelectionState;
class Scene;
class MaterialLibrary;
class TextureLibrary;
class CommandHistory;
class AudioManager;
class PhysicsMaterialLibrary;
struct TimelineBridge;

class InspectorPanel : public EditorPanel
{
public:
    InspectorPanel(SelectionState *selection, Scene *scene,
                   MaterialLibrary *material_library, TextureLibrary *texture_library,
                   CommandHistory *history, AudioManager *audio,
                   const std::function<void *(int, int)> &camera_preview_provider,
                   TimelineBridge *timeline_bridge,
                   PhysicsMaterialLibrary *physics_material_library);
    void OnImGuiRender(float dt) override;

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }
    void ToggleVisible() { m_visible = !m_visible; }

private:
    // Open (once) the undo transaction covering the current entity's property
    // edits. The snapshot is captured when the session opens and committed by
    // EndEditSessionIfReleased when the edited widget is released, so one drag
    // produces exactly one undo step instead of one per frame.
    void BeginEditSession(const char *description);
    // Commit the open session when the last widget was just released.
    void EndEditSessionIfReleased();
    // Run a discrete one-shot edit (combo pick, Apply button, drag-drop assign)
    // inside a single undo transaction; commits immediately.
    void CommitEdit(const char *description, const std::function<void()> &mutate);

    SelectionState *m_selection;
    Scene *m_scene;
    MaterialLibrary *m_material_library;
    TextureLibrary *m_texture_library;
    CommandHistory *m_history;
    AudioManager *m_audio;
    // Recreates the Application-owned camera-preview texture at the requested
    // size and returns its ImTextureID (nullptr when no preview is available).
    std::function<void *(int, int)> m_camera_preview_provider;
    TimelineBridge *m_timeline_bridge;  // nullable: keyframe toggles degrade
    PhysicsMaterialLibrary *m_physics_material_library;  // nullable: .pmat combo degrades
    int m_edit_entity = -1;
    char m_tag_buffer[256] = {};
    char m_mesh_buffer[256] = {};
    char m_script_buffer[256] = {};
    char m_audio_buffer[256] = {};
    char m_new_material_buffer[256] = {};
    int m_last_selected_id = -1;
    bool m_visible = true;
    bool m_new_material_open = false;
    bool m_new_physics_material_open = false;
    char m_new_physics_material_buffer[256] = {};
    float m_new_physics_friction = 0.5f;
    float m_new_physics_restitution = 0.1f;
};
