#include "MaterialPanel.h"

#include "Texture.h"
#include "editor/Theme.h"
#include "editor/UiText.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

// .mat material assets under assets/materials/ (sorted for stable UI).
std::vector<std::string> ListMaterialAssets()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator("assets/materials", ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string path = entry.path().filename().string();
        if (path.size() > 4 && path.substr(path.size() - 4) == ".mat")
            out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Image assets under assets/textures/ (BMP/PNG/JPG by extension).
std::vector<std::string> ListTextureAssets()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator("assets/textures", ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string path = entry.path().filename().string();
        const std::string ext = (path.size() > 4) ? path.substr(path.size() - 4) : std::string();
        if (ext == ".bmp" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".tga" || ext == ".gif")
            out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ImGui's default layout puts the label after the control on the same line,
// reserving only ~1/3 of the row for it -- fine at full window width, but a
// label like "Metallic Multiplier" gets clipped once this panel is docked
// into a ~20%-wide rail. Stacking the label on its own line above a
// full-width control (like Unreal/Unity property panels) stays correct at
// any column width instead of requiring the user to widen the panel.
bool LabeledSlider(const char *label, float *v, float min, float max, const char *fmt = "%.2f")
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    const bool changed = ImGui::SliderFloat("##v", v, min, max, fmt);
    ImGui::PopID();
    return changed;
}

bool LabeledColorEdit4(const char *label, float col[4])
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    const bool changed = ImGui::ColorEdit4("##v", col);
    ImGui::PopID();
    return changed;
}

} // namespace

MaterialPanel::MaterialPanel(MaterialLibrary *material_library,
                             TextureLibrary *texture_library)
    : m_material_library(material_library)
    , m_texture_library(texture_library)
{
}

void MaterialPanel::RefreshList()
{
    m_materials = ListMaterialAssets();

    if (m_selected.empty())
        return;
    const bool still_present =
        std::find(m_materials.begin(), m_materials.end(), m_selected) != m_materials.end();
    if (!still_present)
    {
        m_selected.clear();
        m_dirty = false;
        m_status.clear();
    }
}

void MaterialPanel::Select(const std::string &filename)
{
    if (!m_material_library)
        return;
    std::string error;
    const Material *mat = m_material_library->Load(filename, &error);
    if (!mat)
    {
        m_status = "Load failed: " + error;
        return;
    }
    m_selected = filename;
    m_edit = *mat;
    std::strncpy(m_name_buffer, m_edit.name.c_str(), sizeof(m_name_buffer) - 1);
    m_name_buffer[sizeof(m_name_buffer) - 1] = '\0';
    m_dirty = false;
    m_status.clear();
}

// Reflect the working copy into the MaterialLibrary cache without touching the
// file, so the scene and the Material Preview re-shade on the very next frame.
void MaterialPanel::PushLive()
{
    if (!m_material_library || m_selected.empty())
        return;
    m_edit.name = m_name_buffer;
    m_material_library->LiveUpdate(m_selected, m_edit);
}

void MaterialPanel::SaveEdit()
{
    if (!m_material_library)
        return;
    m_edit.name = m_name_buffer;
    std::string error;
    if (!m_material_library->Save(m_selected, m_edit, &error))
    {
        m_status = "Save failed: " + error;
        return;
    }
    m_dirty = false;
    m_status = "Saved " + m_selected;
}

void MaterialPanel::OpenCreateWizard()
{
    m_wizard_open = true;
    m_wizard_name[0] = '\0';
    m_wizard_color[0] = 1.0f;
    m_wizard_color[1] = 1.0f;
    m_wizard_color[2] = 1.0f;
    m_wizard_color[3] = 1.0f;
    m_wizard_metallic = 0.0f;
    m_wizard_roughness = 0.5f;
}

void MaterialPanel::CreateFromWizard()
{
    if (!m_material_library)
        return;
    std::string name = m_wizard_name;
    if (name.empty())
        return;
    if (name.size() < 4 || name.substr(name.size() - 4) != ".mat")
        name += ".mat";

    Material mat;
    for (int i = 0; i < 4; ++i)
        mat.color[i] = m_wizard_color[i];
    mat.metallic = m_wizard_metallic;
    mat.roughness = m_wizard_roughness;

    std::string error;
    if (!m_material_library->Create(name, mat, &error))
    {
        m_status = "Create failed: " + error;
        return;
    }
    m_wizard_name[0] = '\0';
    ImGui::CloseCurrentPopup();
    Select(name);
    m_status = "Created " + name;
}

// A texture-map slot for one PBR channel: "None" or a file from the texture
// asset list. Changes dirty the working copy and push it live immediately.
void MaterialPanel::DrawTextureSlot(const char *label, std::string &slot)
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
    const char *preview = slot.empty() ? "None" : slot.c_str();
    if (!ImGui::BeginCombo("##v", preview))
    {
        ImGui::PopID();
        return;
    }
    if (ImGui::Selectable("None", slot.empty()))
    {
        slot.clear();
        m_dirty = true;
        PushLive();
    }
    for (const std::string &path : ListTextureAssets())
    {
        const bool selected = (slot == path);
        if (ImGui::Selectable(path.c_str(), selected))
        {
            slot = path;
            m_dirty = true;
            PushLive();
        }
    }
    ImGui::EndCombo();
    ImGui::PopID();
}

void MaterialPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    ImGui::Begin("Material Editor", &m_visible, ImGuiWindowFlags_NoCollapse);

    // This panel spends most of its life docked into a narrow ~20% rail with
    // a dozen-plus stacked fields; the theme's default padding is tuned for
    // full-width panels and wastes vertical room here, forcing more
    // scrolling than the content needs.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));

    RefreshList();

    // Single vertical column: the material list sits in a fixed-height strip
    // above the editor fields instead of a side-by-side split, so the panel
    // stays usable docked into a narrow ~20%-width rail (a fixed-width list
    // pane next to the fields left almost nothing for the fields themselves).
    const float list_h = 140.0f;
    ImGui::BeginChild("##material_list", ImVec2(0.0f, list_h), true);
    ImGui::TextDisabled("Materials");
    ImGui::Separator();
    for (const std::string &filename : m_materials)
    {
        const bool selected = (filename == m_selected);
        if (ImGui::Selectable(filename.c_str(), selected))
            Select(filename);
    }
    ImGui::EndChild();

    ImGui::Spacing();

    ImGui::BeginChild("##material_editor", ImVec2(0.0f, 0.0f), false);

    if (m_selected.empty())
    {
        const float avail_y = ImGui::GetContentRegionAvail().y;
        const float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0.0f, avail_y * 0.30f));

        const char *title = "No Material Selected";
        const float tw = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_x - tw) * 0.5f);
        ImGui::TextDisabled("%s", title);

        const char *hint = "Select a .mat from the list or create a new one.";
        const float hw = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_x - hw) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }
    else
    {
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", m_name_buffer, sizeof(m_name_buffer)))
        {
            m_dirty = true;
            PushLive();
        }
        ImGui::TextDisabled("File: %s", m_selected.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Albedo -------------------------------------------------------
        ImGui::TextUnformatted("Albedo");
        if (LabeledColorEdit4("Diffuse Tint", m_edit.color))
        {
            m_dirty = true;
            PushLive();
        }
        ImGui::TextDisabled("Multiplies the texture; white = un-tinted");

        if (LabeledSlider("Albedo Multiplier", &m_edit.albedo_multiplier, 0.0f, 2.0f))
        {
            m_dirty = true;
            PushLive();
        }
        DrawTextureSlot("Albedo Map", m_edit.texture);

        if (!m_edit.texture.empty())
        {
            if (m_texture_library)
            {
                if (const TextureInfo *info = m_texture_library->Load(m_edit.texture))
                {
                    const float size = ImGui::GetContentRegionAvail().x;
                    float aspect = (float)info->width / (float)std::max(1, info->height);
                    ImGui::Image((ImTextureID)info->texture,
                                 ImVec2(size, size / aspect));
                    ImGui::TextDisabled("%dx%d", info->width, info->height);
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                       "Texture failed to load");
                }
            }
        }
        else
        {
            ImGui::TextDisabled("No texture: flat albedo shading");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Normal (slot-only in the software rasterizer) ----------------
        if (ImGui::CollapsingHeader("Normal", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawTextureSlot("Normal Map", m_edit.normal_texture);
            if (LabeledSlider("Normal Strength", &m_edit.normal_strength, 0.0f, 2.0f))
            {
                m_dirty = true;
                PushLive();
            }
            ImGui::TextDisabled("Slot only: the CPU rasterizer shades flat");
            ImGui::Spacing();
        }

        // --- Metallic -----------------------------------------------------
        if (ImGui::CollapsingHeader("Metallic", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (LabeledSlider("Metallic", &m_edit.metallic, 0.0f, 1.0f))
            {
                m_dirty = true;
                PushLive();
            }
            DrawTextureSlot("Metallic Map", m_edit.metallic_texture);
            if (LabeledSlider("Metallic Multiplier", &m_edit.metallic_multiplier, 0.0f, 2.0f))
            {
                m_dirty = true;
                PushLive();
            }
            ImGui::Spacing();
        }

        // --- Roughness ----------------------------------------------------
        if (ImGui::CollapsingHeader("Roughness", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (LabeledSlider("Roughness", &m_edit.roughness, 0.0f, 1.0f))
            {
                m_dirty = true;
                PushLive();
            }
            DrawTextureSlot("Roughness Map", m_edit.roughness_texture);
            if (LabeledSlider("Roughness Multiplier", &m_edit.roughness_multiplier, 0.0f, 2.0f))
            {
                m_dirty = true;
                PushLive();
            }
            ImGui::TextDisabled("Inverse specular power; 0 = mirror");
            ImGui::Spacing();
        }

        // --- Ambient Occlusion -------------------------------------------
        if (ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (LabeledSlider("AO", &m_edit.ao, 0.0f, 1.0f))
            {
                m_dirty = true;
                PushLive();
            }
            DrawTextureSlot("AO Map", m_edit.ao_texture);
            if (LabeledSlider("AO Multiplier", &m_edit.ao_multiplier, 0.0f, 2.0f))
            {
                m_dirty = true;
                PushLive();
            }
            ImGui::TextDisabled("Scales the ambient light floor");
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();
        if (m_dirty)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f),
                               "Unsaved changes (live preview active)");
            Theme::PushPrimaryButtonColor();
            if (ImGui::Button("Save Material"))
                SaveEdit();
            Theme::PopPrimaryButtonColor();
            ImGui::SameLine();
            if (ImGui::Button("Revert"))
                Select(m_selected);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    Theme::PushPrimaryButtonColor();
    if (ImGui::Button("New Material..."))
        OpenCreateWizard();
    Theme::PopPrimaryButtonColor();
    TextDisabledWrapped("Opens the material authoring wizard");

    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());

    ImGui::EndChild();

    // The compact padding above is specific to this panel's narrow-rail
    // fields; the wizard modal below is a normal-sized floating window and
    // should keep the theme's regular spacing.
    ImGui::PopStyleVar(2);

    // --- Create New Material wizard modal --------------------------------
    if (m_wizard_open)
    {
        ImGui::OpenPopup("Create New Material");
        m_wizard_open = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Create New Material", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("File Name", m_wizard_name, sizeof(m_wizard_name));
        ImGui::TextDisabled("Saved as .mat in assets/materials/");
        ImGui::Spacing();
        ImGui::ColorEdit4("Albedo Tint", m_wizard_color);
        ImGui::SliderFloat("Metallic", &m_wizard_metallic, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Roughness", &m_wizard_roughness, 0.0f, 1.0f, "%.2f");
        ImGui::Spacing();
        Theme::PushPrimaryButtonColor();
        if (ImGui::Button("Create") && m_wizard_name[0] != '\0')
            CreateFromWizard();
        Theme::PopPrimaryButtonColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}
