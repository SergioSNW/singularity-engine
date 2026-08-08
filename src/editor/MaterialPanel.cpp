#include "MaterialPanel.h"

#include "Texture.h"
#include "editor/Theme.h"

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

void MaterialPanel::CreateNew()
{
    if (!m_material_library)
        return;
    std::string name = m_new_name;
    if (name.empty())
        return;
    if (name.size() < 4 || name.substr(name.size() - 4) != ".mat")
        name += ".mat";

    Material mat;
    mat.color[0] = 1.0f;
    mat.color[1] = 1.0f;
    mat.color[2] = 1.0f;
    mat.color[3] = 1.0f;

    std::string error;
    if (!m_material_library->Create(name, mat, &error))
    {
        m_status = "Create failed: " + error;
        return;
    }
    m_new_name[0] = '\0';
    Select(name);
    m_status = "Created " + name;
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

void MaterialPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    ImGui::Begin("Material Editor", &m_visible, ImGuiWindowFlags_NoCollapse);

    RefreshList();

    const float list_w = 170.0f;
    ImGui::BeginChild("##material_list", ImVec2(list_w, 0.0f), true);
    ImGui::TextDisabled("Materials");
    ImGui::Separator();
    for (const std::string &filename : m_materials)
    {
        const bool selected = (filename == m_selected);
        if (ImGui::Selectable(filename.c_str(), selected))
            Select(filename);
    }
    ImGui::EndChild();

    ImGui::SameLine();

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

        const char *hint = "Select a .mat from the list or create a new one below.";
        const float hw = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_x - hw) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }
    else
    {
        if (ImGui::InputText("Name", m_name_buffer, sizeof(m_name_buffer)))
            m_dirty = true;
        ImGui::TextDisabled("File: %s", m_selected.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::ColorEdit4("Diffuse Tint", m_edit.color))
            m_dirty = true;
        ImGui::TextDisabled("Multiplies the texture; white = un-tinted");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const char *tex_preview = m_edit.texture.empty() ? "None" : m_edit.texture.c_str();
        if (ImGui::BeginCombo("Texture", tex_preview))
        {
            if (ImGui::Selectable("None", m_edit.texture.empty()))
            {
                m_edit.texture.clear();
                m_dirty = true;
            }
            for (const std::string &path : ListTextureAssets())
            {
                bool selected = (m_edit.texture == path);
                if (ImGui::Selectable(path.c_str(), selected))
                {
                    m_edit.texture = path;
                    m_dirty = true;
                }
            }
            ImGui::EndCombo();
        }

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

        if (ImGui::SliderFloat("Shininess", &m_edit.shininess, 0.0f, 1.0f, "%.2f"))
            m_dirty = true;

        ImGui::Spacing();
        if (m_dirty)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "Unsaved changes");
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

    ImGui::TextUnformatted("New Material");
    ImGui::InputText("File Name", m_new_name, sizeof(m_new_name));
    Theme::PushPrimaryButtonColor();
    if (ImGui::Button("Create") && m_new_name[0] != '\0')
        CreateNew();
    Theme::PopPrimaryButtonColor();
    ImGui::SameLine();
    ImGui::TextDisabled("Creates a white .mat in assets/materials/");

    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());

    ImGui::EndChild();
    ImGui::End();
}
