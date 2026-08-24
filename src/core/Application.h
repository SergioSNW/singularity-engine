#pragma once

#include "core/ToastManager.h"
#include "core/ViewportOverlaySettings.h"
#include "core/Profiler.h"

#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

#include "Json.h"
#include "editor/Theme.h"
#include "core/WorkspaceManager.h"
#include "core/Animation.h"
#include "editor/GizmoController.h"
#include "core/EditorCamera.h"
#include "core/Landscape.h"
#include "core/Environment.h"
#include "core/Material.h"
#include "render/EnvironmentFX.h"

class EnvironmentPanel;
class MaterialPreviewPanel;

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
class PhysicsMaterialLibrary;
class CollisionMatrixPanel;
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
class SceneHierarchyPanel;
class StatsPanel;
class MaterialPanel;
class ViewportLayoutPanel;
class LandscapePanel;
class TimelinePanel;
class CommandHistory;
class HistoryPanel;
class ProfilerPanel;
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
    // `draw_calls` accumulates the SDL draw calls issued by the pass (line
    // primitives + geometry batches) for the Profiler's resource readout.
    void RenderScenePass(SDL_Renderer *renderer, const Mat4 &view_proj,
                         float near_p, int w, int h, Entity *skip_entity,
                         const Vec3 &cam_pos, int &draw_calls);
    void RenderEditorOverlay(SDL_Renderer *renderer, const Mat4 &view_proj,
                             const EditorCamera &pose, float near_p, int w, int h,
                             int &draw_calls);

    // Placement-mode / drag-drop ghost preview: a translucent cyan wireframe
    // of whatever mesh is about to be placed (armed via placement mode, or
    // being dragged from the Content Browser), tracking the landscape/ground
    // raycast hit under the mouse every frame. See UpdateAssetPlacement for
    // the click-to-commit half of this workflow.
    void RenderPlacementGhost(SDL_Renderer *renderer, const Mat4 &view_proj,
                              float near_p, int w, int h);
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

    // Phase 38 material preview: render the active material's test mesh (UV
    // sphere or cylinder) into an off-screen target each frame under the
    // environment lighting; the Material Preview panel draws it live and its
    // orbit state drives the framing.
    void RecreateMaterialPreview(int width, int height);
    void RenderMaterialPreview();

    // Resolve the PBR shading scalars that shade `entity`: the assigned .mat
    // asset's metallic/roughness/ao/albedo-multiplier, or the defaults when
    // the entity has no material asset.
    MaterialShading ResolveEntityShading(const Entity &entity) const;

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

    // Phase 34 landscape & topology design:
    //   ResolveEntityMesh    - the mesh that renders/picks an entity: a
    //                          landscape's generated mesh when enabled, else
    //                          the standard asset/builtin resolution.
    //   CreateLandscape      - spawn a sculptable heightfield entity (undoable),
    //                          select it and arm it as the brush target.
    //   IsLandscapeSculptMode- true when the Landscape workspace is active and
    //                          the brush has a live landscape target (the
    //                          viewport override applies).
    //   UpdateLandscapeBrush - the editor-interaction replacement for the
    //                          gizmo: track the brush cursor on the terrain and
    //                          apply paint strokes while LMB is held.
    const Mesh *ResolveEntityMesh(const Entity &entity);
    Entity *CreateLandscape();
    bool IsLandscapeSculptMode() const;
    void UpdateLandscapeBrush(const GizmoFrame &gf, float dt);

    // Asset placement mode: a modal alternative to the gizmo (like landscape
    // sculpting) for spawning many copies of one asset quickly. Armed via the
    // Content Browser's "Place in Scene" context menu item, which sets the
    // pending asset and turns the mode on; toggled off from the toolbar or
    // Escape. While active, a viewport click spawns the armed mesh/prefab at
    // the raycast hit point (landscape surface, or the y=0 plane if none).
    void UpdateAssetPlacement(const GizmoFrame &gf);

    // Phase 35 animation & timeline foundation:
    //   ApplyTimeline        - editor Update stage: advance the global clock
    //                          (wrap/clamp per Loop) and write the sampled pose
    //                          onto every entity carrying keyframes, but only
    //                          while playing or right after a scrub/record so
    //                          gizmo/Inspector edits are never stomped.
    //   PlayPauseTimeline    - toggle the transport (restarts from 0 when the
    //                          playhead sits at the end).
    //   StopTimeline         - halt the transport and rewind to 0.
    //   ScrubTimeline        - re-apply the pose at the moved playhead.
    //   SetTimelineKeyframe  - record the selected entity's property value at
    //                          the current playhead (undoable), extending the
    //                          global duration when the key lands past it.
    //   RemoveTimelineKeyframe - drop the key exactly at `time` (undoable) and
    //                          refresh the entity's duration.
    //   FindTimelineTarget   - the selected entity, or null.
    //   ApplyWorkspace       - workspace switch wrapper: routes the Script
    //                          Editor dock node AND applies workspace side
    //                          effects (viewport visibility + timeline state).
    //   ResetWorkspaceDefault- reset wrapper with the same side effects.
    //   SyncWorkspaceSideEffects - hide the viewport in Sequencing and stop
    //                          timeline playback when leaving it.
    void ApplyTimeline(float dt);
    void PlayPauseTimeline();
    void StopTimeline();
    void ScrubTimeline();
    void SetTimelineKeyframe(AnimProperty prop);
    void RemoveTimelineKeyframe(AnimProperty prop, float time);
    Entity *FindTimelineTarget() const;
    unsigned int ApplyWorkspace(WorkspaceManager::Workspace ws);
    unsigned int ResetWorkspaceDefault();
    void SyncWorkspaceSideEffects(WorkspaceManager::Workspace ws);

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
    // `display_name`, when non-null, overrides the tag derived from the mesh
    // path's filename -- used for builtin primitives ("__builtin_wall__"
    // would otherwise become the entity's literal tag).
    void SpawnMeshEntity(const std::string &mesh_path, const Vec3 &position,
                         const char *display_name = nullptr);
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
    PhysicsMaterialLibrary *m_physics_material_library;
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
    SceneHierarchyPanel *m_hierarchy_panel;
    StatsPanel *m_stats_panel;
    ViewportLayoutPanel *m_viewport_layout_panel;
    MaterialPanel *m_material_panel;
    LandscapePanel *m_landscape_panel;
    TimelinePanel *m_timeline_panel;
    CollisionMatrixPanel *m_collision_matrix_panel;
    EnvironmentPanel *m_environment_panel;      // Phase 37: sky/fog/post UI
    MaterialPreviewPanel *m_material_preview_panel;  // Phase 38: preview viewport
    CommandHistory *m_history;       // global undo/redo stack (Phase 22)
    HistoryPanel *m_history_panel;   // read-only view over m_history
    ProfilerPanel *m_profiler_panel; // live performance telemetry UI (Phase 30)
    SDL_Texture *m_viewport_target;
    int m_viewport_target_w;
    int m_viewport_target_h;
    SDL_Texture *m_camera_preview;   // Inspector live camera preview target
    int m_camera_preview_w;
    int m_camera_preview_h;
    SDL_Texture *m_material_preview; // Phase 38: material preview target
    int m_material_preview_w;
    int m_material_preview_h;
    float m_camera_scroll;
    float m_ui_scale;
    float m_applied_ui_scale;
    float m_dpi_scale;
    Theme::Fonts m_fonts;
    Theme::Colors m_theme_colors;
    WorkspaceManager m_workspace_manager;
    WorkspaceManager::Workspace m_previous_workspace =
        WorkspaceManager::Workspace::LevelDesign;
    Vec3 m_saved_camera_pos{ 0.0f, 2.0f, 8.0f };
    float m_saved_camera_pitch = -14.0f;
    float m_saved_camera_yaw = 0.0f;
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
    bool m_collision_matrix_panel_was_visible;
    bool m_environment_panel_was_visible;
    bool m_material_preview_panel_was_visible;

    // Phase 37 environment stack: the global settings (sky/fog/post) that the
    // EnvironmentFX pass consumes every frame. m_fx owns all of its SDL
    // textures/buffers and caches the sky + LUT across frames.
    //
    // m_fx serves the main viewport; m_fx_preview serves the Material
    // Preview panel. They must NOT share one instance: EnvironmentFX caches
    // its buffers by region size, and the preview's region is a different
    // size from the viewport's. A shared instance would destroy/recreate its
    // (now full-resolution, since post_scale only sizes bloom) work texture
    // and buffers every single frame whenever both are visible in the same
    // workspace -- exactly what "Shading & Assets" does by design.
    EnvironmentSettings m_environment;
    EnvironmentFX m_fx;
    EnvironmentFX m_fx_preview;
    std::string m_environment_asset_path;

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

    // Phase 30: real-time performance telemetry. Run() drives StartFrame /
    // BeginStage / EndStage / RecordResources every frame; the ProfilerPanel
    // renders the rolling series. m_draw_calls accumulates the SDL draw calls
    // issued by the 3D passes each frame (reset at frame start).
    Profiler m_profiler;
    int m_draw_calls = 0;
    double m_last_diagnostic_time = 0.0;  // last time diagnostics were logged

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

    // Phase 34 landscape sculpting: the shared brush settings (edited by the
    // Landscape panel), the last valid brush-cursor hit for the viewport
    // overlay, and the in-stroke flag that brackets each paint stroke as one
    // undo transaction.
    LandscapeBrushSettings m_landscape_brush;
    bool m_landscape_brush_valid = false;
    Vec3 m_landscape_brush_center{0.0f, 0.0f, 0.0f};
    bool m_landscape_sculpting = false;

    // Asset placement mode (see UpdateAssetPlacement): the armed asset and
    // whether it's a prefab (LoadPrefab) or a bare mesh (SpawnMeshEntity).
    bool m_placement_mode = false;
    std::string m_placement_asset_path;
    bool m_placement_is_prefab = false;

    // Phase 35 animation & timeline foundation: the Application-owned global
    // timeline clock + the bridge the Timeline panel and the Inspector's
    // keyframe toggles share. m_timeline_dirty is set after any scrub/record
    // so ApplyTimeline re-writes poses even while paused.
    TimelineState m_timeline;
    TimelineBridge m_timeline_bridge;
    bool m_timeline_dirty = false;
};
