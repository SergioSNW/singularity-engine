#pragma once

#include "EditorPanel.h"
#include "core/Material.h"

#include <string>
#include <vector>

class TextureLibrary;

// Dedicated dockable material authoring panel (Phase 21, reworked Phase 38).
//
// A two-pane editor over assets/materials/: a scrollable asset list on the
// left, and a property editor on the right. Phase 38 promotes the material
// into explicit PBR channels — albedo tint + albedo map + multiplier, plus
// Normal / Metallic / Roughness / Ambient-Occlusion scalars with optional
// texture-map slots and channel multipliers — so the panel now mirrors the
// .mat schema directly. Every edit is pushed to the MaterialLibrary in memory
// (LiveUpdate) so both the live scene and the Material Preview viewport render
// the new values immediately; Save persists the .mat to disk. New assets are
// authored through the "Create New Material" wizard (also reachable from the
// command palette and the workspace's New Material… button).
//
// In the Shading & Assets workspace the panel is the primary authoring zone
// (full right rail) beside the dedicated Material Preview viewport; in the
// other workspaces it docks as a tab in the bottom development zone.
class MaterialPanel : public EditorPanel
{
public:
    MaterialPanel(MaterialLibrary *material_library, TextureLibrary *texture_library);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    // Currently authored .mat filename (empty when nothing is selected); the
    // Material Preview reads the library copy of this asset each frame.
    const std::string &Selected() const { return m_selected; }

    // Open the "Create New Material" wizard modal.
    void OpenCreateWizard();

private:
    void RefreshList();
    void Select(const std::string &filename);
    void SaveEdit();
    void PushLive();  // in-memory cache update so scene + preview re-shade now
    void CreateFromWizard();
    void DrawTextureSlot(const char *label, std::string &slot);

    MaterialLibrary *m_material_library;
    TextureLibrary *m_texture_library;

    std::vector<std::string> m_materials;  // sorted .mat filenames in assets/materials/
    std::string m_selected;                // currently authored .mat filename
    Material m_edit;                       // working copy of the selected material
    bool m_dirty = false;                  // unsaved edits pending
    char m_name_buffer[128] = {};          // edit buffer for the material name
    std::string m_status;                  // last-action feedback line
    bool m_visible = true;

    bool m_wizard_open = false;            // "Create New Material" modal pending
    char m_wizard_name[128] = {};          // wizard file-name buffer
    float m_wizard_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float m_wizard_metallic = 0.0f;
    float m_wizard_roughness = 0.5f;
};
