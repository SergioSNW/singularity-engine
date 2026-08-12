#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Json.h"
#include "editor/Theme.h"
#include "core/WorkspaceManager.h"
#include "editor/GizmoController.h"
#include "core/EditorCamera.h"

struct SDL_Texture;
struct Mat4;
struct Vec3;
struct Vec2;
struct Mesh;
class Window;
class EditorPanel;
struct SelectionState;
class ViewportPanel;
class Scene;
class SceneManager;
class MeshLibrary;
class MaterialLibrary;
class TextureLibrary;
class GizmoController;
class ScriptEngine;
class PhysicsManager;
class AudioManager;
class ScriptEditorPanel;
class CommandPalette;
class SettingsPanel;
class ContentBrowserPanel;
class ConsolePanel;
class InspectorPanel;
class MaterialPanel;
class CommandHistory;
class HistoryPanel;
struct Entity;

// Editor runtime state machine. Play mode isolates the viewport as a full-window
// game view and snapshots the scene so Stop restores it exactly.
enum class EngineState
{
    Editor,
    Play,
};

class Application
{
public:
    Application();
    ~Application();

    bool Init(int width, int height, const char *title);
    void Run();
    void Shutdown();

    // --- Scene management (driveable from panels such as the Content Browser) ---
    // Load a map file into the active scene (editor only). Clears the
    // selection and repoints m_scene_path on success.
    void LoadSceneFile(const std::string &filepath);
    // Start from a blank map. Active path is cleared until the next save.
    void NewScene();
    // Open the "Save Scene As" modal (writes assets/scenes/<name>.json).
    void OpenSaveAsModal();

private:
    void RecreateViewportTarget(int width, int height);
    void RenderViewportTarget();
    void UpdateCameraControls(float dt);
    Entity *FindActiveCamera();
    bool BuildViewProj(Mat4 &view_proj, Vec3 &cam_pos, float &fov, float &pitch,
                       float &yaw, float &near_p, float &far_p);

    // Phase 25 free-fly editor camera: the pose the editor renders the
    // viewport through, independent of the scene's gameplay camera entities.
    //   CaptureGameplayCamera  - read the active gameplay camera entity's pose.
    //   GetActiveCameraPose    - the pose to render with this frame: the editor
    //                            camera in editor mode, the gameplay camera in
    //                            play mode, or a smooth blend during transition.
    //   BeginCameraTransition  - kick off the Play/Stop blend between the two.
    //   UpdateCameraTransition - advance an in-flight blend by dt.
    bool CaptureGameplayCamera(EditorCamera &out);
    bool GetActiveCameraPose(EditorCamera &out);
    void BeginCameraTransition(CameraTransitionPhase phase);
    void UpdateCameraTransition(float dt);
    const Mesh *ResolveMesh(const Entity &entity, std::string &error);
    void SaveScene();
    void OpenScene();
    void DrawSaveAsModal();
    void EnterPlayMode();
    void ExitPlayMode();

    // Duplicate the selected entity (and its whole subtree) as a sibling under
    // its current parent, then select the clone. No-op without a selection.
    void DuplicateSelection();

    // Phase 23 drop ingestion:
    //   ProcessExternalDrops   - routes queued SDL_DROPFILE batches into the
    //                            assets tree after the frame (Content Browser
    //                            refresh), and spawns mesh entities at the
    //                            drop point when it lands over the viewport.
    //   SpawnMeshEntity        - create + select an entity carrying `mesh_path`.
    //   ComputeDropWorldPos    - unproject a viewport pixel onto the y=0 grid
    //                            plane (same ray math as the gizmo controller).
    //   ComputeDropWorldPosFromMouse - convenience: drop pos from GetMousePos.
    void ProcessExternalDrops();
    void SpawnMeshEntity(const std::string &mesh_path, const Vec3 &position);
    bool ComputeDropWorldPos(float sx, float sy, float vp_w, float vp_h, Vec3 &out);
    bool ComputeDropWorldPosFromMouse(Vec3 &out);

    // Resolve the texture that should shade `entity`: honors an assigned .mat
    // asset first, then a direct texture_path, then returns nullptr (flat
    // shading). `out_tint` receives the effective RGBA tint (asset color or
    // the entity's own color) and `out_uvs` the mesh UVs when usable.
    SDL_Texture *ResolveEntityTexture(const Entity &entity, const Mesh &mesh,
                                      const float *&out_tint, const std::vector<Vec2> *&out_uvs);
    SDL_Texture *ResolveEntityTexture(const Entity &entity);

    // Play-mode panel isolation: snapshot the visibility of the non-essential
    // editor windows (script editor, content browser, console, inspector) and
    // hide them so play mode renders a clean game view; restore the snapshot
    // immediately on exit.
    void SavePlayModePanelState();
    void RestorePlayModePanelState();

    Window *m_window;
    bool m_running;
    bool m_flying;
    SelectionState *m_selection;
    ViewportPanel *m_viewport;
    Scene *m_scene;                 // always == m_scene_manager->GetScene()
    SceneManager *m_scene_manager;
    MeshLibrary *m_mesh_library;
    MaterialLibrary *m_material_library;
    TextureLibrary *m_texture_library;
    GizmoController *m_gizmo;
    ScriptEngine *m_script_engine;
    PhysicsManager *m_physics;
    AudioManager *m_audio;
    ScriptEditorPanel *m_script_editor;
    CommandPalette *m_command_palette;
    SettingsPanel *m_settings_panel;
    ContentBrowserPanel *m_content_browser;
    ConsolePanel *m_console_panel;
    InspectorPanel *m_inspector_panel;
    MaterialPanel *m_material_panel;
    CommandHistory *m_history;       // global undo/redo stack (Phase 22)
    HistoryPanel *m_history_panel;   // read-only view over m_history
    SDL_Texture *m_viewport_target;
    int m_viewport_target_w;
    int m_viewport_target_h;
    float m_camera_scroll;
    float m_ui_scale;
    float m_applied_ui_scale;
    float m_dpi_scale;
    Theme::Fonts m_fonts;
    Theme::Colors m_theme_colors;
    WorkspaceManager m_workspace_manager;
    bool m_recreate_viewport;
    std::string m_scene_path;
    std::string m_scene_status;
    std::string m_mesh_error;
    EngineState m_state;
    json::Value m_scene_snapshot;   // pre-play backup; restored on Stop
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
    bool m_save_as_open;            // "Save Scene As" modal is pending
    char m_save_as_name[128] = {};  // file name typed into that modal
    bool m_play_panel_saved;        // play-mode panel snapshot is valid
    bool m_script_editor_was_visible;
    bool m_content_browser_was_visible;
    bool m_console_was_visible;
    bool m_inspector_was_visible;
    bool m_material_panel_was_visible;
    bool m_history_panel_was_visible;

    // Editor grid-snapping configuration (Phase 18): steps for the translate/
    // rotate/scale gizmos, plus the persistent snap toggle. Wire
    // GizmoFrame::snap_active = m_snap.enabled || Ctrl-held.
    SnapSettings m_snap;

    // Phase 25 free-fly editor camera + navigation tuning. m_editor_camera is
    // the viewport pose the editor renders through and Fly Mode drives;
    // m_camera_transition is the in-flight Play/Stop blend toward/away from the
    // active gameplay camera.
    EditorCamera m_editor_camera;
    EditorCameraSettings m_camera_settings;
    CameraTransitionState m_camera_transition;

    // OS file-drop queue (Phase 23): SDL_DROPFILE events push their paths here
    // during the event pump; the editor processes the batch after the ImGui
    // frame has drawn (routing into the assets tree, spawning viewport meshes).
    std::vector<std::string> m_pending_drops;
};
