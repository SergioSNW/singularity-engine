#pragma once

#include "EditorPanel.h"
#include "core/Material.h"

#include <string>
#include <vector>

class TextureLibrary;

// Dedicated dockable material authoring panel (Phase 21).
//
// A two-pane editor over assets/materials/: a scrollable asset list on the
// left, and a property editor on the right with diffuse color tint, texture
// slot selection, a live ImGui::Image preview, and a shininess knob. Edits are
// applied to a working copy and persisted with Save (rewrites the .mat and
// refreshes the MaterialLibrary cache so meshes re-tint immediately); new
// materials are created from the current tint through MaterialLibrary::Create.
//
// In the Shading & Assets workspace the panel is the primary authoring zone
// (full right rail); in the other workspaces it docks as a tab in the bottom
// development zone so it is always one click away but never floats.
class MaterialPanel : public EditorPanel
{
public:
    MaterialPanel(MaterialLibrary *material_library, TextureLibrary *texture_library);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    void RefreshList();
    void Select(const std::string &filename);
    void CreateNew();
    void SaveEdit();

    MaterialLibrary *m_material_library;
    TextureLibrary *m_texture_library;

    std::vector<std::string> m_materials;  // sorted .mat filenames in assets/materials/
    std::string m_selected;                // currently authored .mat filename
    Material m_edit;                       // working copy of the selected material
    bool m_dirty = false;                  // unsaved edits pending
    char m_name_buffer[128] = {};          // edit buffer for the material name
    char m_new_name[128] = {};             // "New Material" file-name buffer
    std::string m_status;                  // last-action feedback line
    bool m_visible = true;
};
