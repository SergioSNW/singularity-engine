#pragma once

// Phase 29 — viewport overlay & gizmo toolbar settings.
//
// One pure, headless-testable struct shared by the editor chrome and the
// render passes: the viewport header toolbar, the View menu, and the command
// palette all edit a single instance, while RenderScenePass / RenderEditorOverlay
// read it to decide what gets drawn. Deliberately free of any UI/render
// dependency so the harness can exercise it standalone.
enum class ViewportRenderMode
{
    Lit = 0,        // lit solid fills + wireframe overlay pass
    Wireframe = 1,  // no solid fills; wireframe overlay only
    Unlit = 2,      // flat-albedo fills (no light shading, no wireframe)
};

struct ViewportOverlaySettings
{
    ViewportRenderMode render_mode = ViewportRenderMode::Lit;

    // Overlay visibility toggles (all on by default except the stats HUD).
    bool grid = true;          // ground-plane grid
    bool colliders = true;     // physics collider volumes
    bool light_gizmos = true;  // directional-light indicators
    bool bounds = true;        // selection/hover bounds boxes
    bool gizmo = true;         // transform gizmo handles
    bool hud = false;          // on-viewport FPS + camera stats overlay

    void SetRenderMode(ViewportRenderMode mode) { render_mode = mode; }

    // Advance Lit -> Wireframe -> Unlit -> Lit.
    ViewportRenderMode NextRenderMode() const
    {
        int next = (int)render_mode + 1;
        if (next > (int)ViewportRenderMode::Unlit)
            next = (int)ViewportRenderMode::Lit;
        return (ViewportRenderMode)next;
    }

    static const char *RenderModeLabel(ViewportRenderMode mode)
    {
        switch (mode)
        {
            case ViewportRenderMode::Wireframe: return "Wireframe";
            case ViewportRenderMode::Unlit:     return "Unlit";
            case ViewportRenderMode::Lit:       return "Lit";
        }
        return "Lit";
    }
};
