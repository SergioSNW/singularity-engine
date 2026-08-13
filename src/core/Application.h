#pragma once

#include "core/ToastManager.h"
#include "core/ViewportOverlaySettings.h"

#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

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
struct CameraEntry;
class CameraManager;
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
class ViewportLayoutPanel;
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
    Entity *FindActiveCamera() const;
    bool BuildViewProj(Mat4 &view_proj, Vec3 &cam_pos, float &fov, float &pitch,
                       float &yaw, float &near_p, float &far_p);

    // Phase 27 camera manager plumbing.
    //   RenderScenePass       - the shared scene render (grid, solid fills,
    //                            wireframe) for one viewport region. Used by
    //                            the multi-pass viewport render and the
    //                            Inspector camera preview.
    //   CaptureSceneCamera    - read any camera entity's pose (generalizes
    //                            CaptureGameplayCamera).
    //   ResolveCameraPose     - the pose a CameraEntry renders with: the
    //                            editor/blended camera for Editor sources, the
    //                            referenced entity for SceneEntity sources.
    //   BuildViewProjFromPose - build view_proj from an explicit pose + clip
    //                            planes + aspect (no scene dependence).
    //   GetPrimarySkipEntity  - the entity hidden in the primary viewport pass
    //                            (its own camera), used for picking.
    //   GetPrimaryViewportRect- pixel rect of the primary entry in the render
    //                            target, for mouse->world mapping.
    void RenderScenePass(SDL_Renderer *renderer, const Mat4 &view_proj,
                         float near_p, int w, int h, Entity *skip_entity);
    void RenderEditorOverlay(SDL_Renderer *renderer, const Mat4 &view_proj,
                             const EditorCamera &pose, float near_p, int w, int h);
    bool CaptureSceneCamera(Entity &camera_entity, EditorCamera &out);
    bool ResolveCameraPose(const CameraEntry &entry, EditorCamera &out);
    bool BuildViewProjFromPose(const EditorCamera &pose, float near_p,
                               float far_p, float aspect, Mat4 &view_proj);
    Entity *GetPrimarySkipEntity() const;
    bool GetPrimaryViewportRect(int &px, int &py, int &pw, int &ph) const;

    // Phase 27 camera preview: render the selected camera entity into a small
    // off-screen target each editor frame; the Inspector draws it live.
    void RecreateCameraPreview(int width, int height);
    void RenderCameraPreview();

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

    // Phase 28 editor chrome:
    //   DrawStatusBar   - bottom full-width strip with workspace, scene status,
    //                     and live metrics (FPS, audio channels, viewports,
    //                     entity count). Drawn in editor mode each frame.
    //   DrawToasts      - top-right notification overlay fed by PushToast().
    //   DrawViewportContextMenu - right-click popup over the 3D view (rename /
    //                     duplicate / delete / primitives).
    //   SpawnPrimitive  - create + select an undoable editor primitive (empty
    //                     entity, or a mesh/material-bearing spawn).
    //   DeleteSelection  - undoable delete of the current selection.
    void DrawStatusBar(float dt);
    void DrawToasts();
    void DrawViewportContextMenu();
    void PushToast(const std::string &text);
    Entity *SpawnPrimitive(const char *label, const char *mesh_path,
                           const char *material_path);
    void DeleteSelection();

    // Phase 29 viewport overlays & gizmo toolbar:
    //   DrawViewportToolbar - the docked header bar inside the Viewport window
    //                         (render modes, overlay toggles, snapping quick
    //                         controls). Wired as ViewportPanel::on_toolbar.
    //   DrawViewportHud     - on-viewport stats overlay (FPS + camera pose),
    //                         wired as ViewportPanel::on_overlay.
    void DrawViewportToolbar();
    void DrawViewportHud();

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
    CameraManager *m_cameras;
    ScriptEditorPanel *m_script_editor;
    CommandPalette *m_command_palette;
    SettingsPanel *m_settings_panel;
    ContentBrowserPanel *m_content_browser;
    ConsolePanel *m_console_panel;
    InspectorPanel *m_inspector_panel;
    ViewportLayoutPanel *m_viewport_layout_panel;
    MaterialPanel *m_material_panel;
    CommandHistory *m_history;       // global undo/redo stack (Phase 22)
    HistoryPanel *m_history_panel;   // read-only view over m_history
    SDL_Texture *m_viewport_target;
    int m_viewport_target_w;
    int m_viewport_target_h;
    SDL_Texture *m_camera_preview;   // Inspector live camera preview target
    int m_camera_preview_w;
    int m_camera_preview_h;
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

    // Phase 28: smoothed FPS + toast overlay + status bar + viewport context
    // menu state.
    float m_fps = 0.0f;              // exponentially-smoothed editor FPS
    ToastManager m_toasts;           // push via PushToast(), drawn by DrawToasts()
    bool m_status_bar_visible = true;// View menu toggle (rebuilds the dock on change)

    // Phase 29: viewport overlay & gizmo toolbar state. One instance is shared
    // by the header toolbar, the View menu, and the command palette; the render
    // passes read it each frame. Snap steps live in m_snap (GizmoController.h).
    ViewportOverlaySettings m_overlay;

    // RMB over the viewport is a two-way gesture: a quick click opens the
    // context menu; press-and-move (past kViewportRmbFlyThreshold px) captures
    // the cursor and enters Fly Mode.
    bool m_viewport_rmb_pending = false;
    float m_viewport_rmb_down_x = 0.0f;
    float m_viewport_rmb_down_y = 0.0f;

    // Viewport "Rename..." modal state (harmonized with the Hierarchy row).
    bool m_viewport_rename_open = false;
    int m_viewport_rename_entity = -1;
    char m_viewport_rename_buffer[128] = {};
};
