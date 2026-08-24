#include "LandscapePanel.h"

#include "core/Entity.h"
#include "core/Landscape.h"
#include "core/Scene.h"
#include "SelectionState.h"
#include "editor/Theme.h"
#include "editor/UiText.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace
{

// Image assets under assets/textures/ (BMP/PNG/JPG/...) -- heightmaps are
// grayscale-intensity images, so they live alongside regular textures rather
// than needing their own asset folder. Matches MaterialPanel's
// ListTextureAssets() convention.
std::vector<std::string> ListHeightmapAssets()
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

LandscapePanel::LandscapePanel(Scene *scene, SelectionState *selection,
                               LandscapeBrushSettings *brush,
                               std::function<void()> on_create_landscape)
    : m_scene(scene)
    , m_selection(selection)
    , m_brush(brush)
    , m_on_create_landscape(std::move(on_create_landscape))
{
}

void LandscapePanel::OnImGuiRender(float dt)
{
    (void)dt;
    if (!m_visible)
        return;
    ImGui::Begin("Landscape");

    if (!m_scene)
    {
        ImGui::End();
        return;
    }

    int terrain_count = 0;
    for (auto &e : m_scene->GetEntities())
        if (e->landscape.enabled)
            ++terrain_count;

    // First-use guide: the panel's job is sculpting, but it still needs a
    // terrain to sculpt. One click spawns a default heightfield (undoable).
    if (terrain_count == 0)
    {
        ImGui::TextWrapped("No landscape terrain in the scene yet.");
        ImGui::Spacing();
        if (ImGui::Button("Create Landscape", ImVec2(-1.0f, 0.0f)))
        {
            if (m_on_create_landscape)
                m_on_create_landscape();
        }
        ImGui::Spacing();
        ImGui::TextDisabled(
            "A landscape is a sculptable heightfield grid. Create one, then\n"
            "paint terrain with the brush tools below.");
        ImGui::End();
        return;
    }

    // --- Target ----------------------------------------------------------
    const char *target_name = "None";
    Entity *target = nullptr;
    if (m_brush)
    {
        target = m_scene->GetEntityById(m_brush->target_id);
        if (!target || !target->landscape.enabled)
        {
            target = nullptr;
            m_brush->target_id = -1;
        }
        if (target)
            target_name = target->tag.tag.c_str();
    }
    if (ImGui::BeginCombo("Target", target_name))
    {
        for (auto &e : m_scene->GetEntities())
        {
            if (!e->landscape.enabled)
                continue;
            const bool is_current = m_brush && m_brush->target_id == e->id;
            if (ImGui::Selectable(e->tag.tag.c_str(), is_current))
            {
                if (m_brush)
                    m_brush->target_id = e->id;
                if (m_selection)
                {
                    m_selection->entity_id = e->id;
                    m_selection->entity_name = e->tag.tag;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("+"))
    {
        if (m_on_create_landscape)
            m_on_create_landscape();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create another landscape terrain");

    ImGui::Separator();

    // --- Heightmap import (Phase A) --------------------------------------
    // Collapsed by default so the panel stays minimal when you're just
    // sculpting -- most sessions won't touch this every time.
    if (target && ImGui::CollapsingHeader("Heightmap"))
    {
        ImGui::PushID("heightmap");

        const char *preview = m_heightmap_pending_path.empty()
            ? "Select image..." : m_heightmap_pending_path.c_str();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##source", preview))
        {
            const std::vector<std::string> images = ListHeightmapAssets();
            if (images.empty())
                ImGui::TextDisabled("No images in assets/textures/");
            for (const std::string &img : images)
            {
                const bool selected = (img == m_heightmap_pending_path);
                if (ImGui::Selectable(img.c_str(), selected))
                    m_heightmap_pending_path = img;
            }
            ImGui::EndCombo();
        }

        ImGui::SliderFloat("Scale", &target->landscape.heightmap_scale, 0.1f, 50.0f, "%.1f");
        ImGui::SliderInt("Resolution", &m_heightmap_target_resolution,
                         kMinHeightmapResolution, kMaxHeightmapResolution);

        const bool can_load = !m_heightmap_pending_path.empty();
        ImGui::BeginDisabled(!can_load);
        Theme::PushPrimaryButtonColor();
        if (ImGui::Button("Load Heightmap", ImVec2(-1.0f, 0.0f)))
        {
            std::string load_error;
            if (LandscapeLoadHeightmap(target->landscape, m_heightmap_pending_path,
                                       m_heightmap_target_resolution, &load_error))
                m_heightmap_status = "Loaded " + m_heightmap_pending_path + " (" +
                                     std::to_string(m_heightmap_target_resolution + 1) + "x" +
                                     std::to_string(m_heightmap_target_resolution + 1) + ")";
            else
                m_heightmap_status = load_error;
        }
        Theme::PopPrimaryButtonColor();
        ImGui::EndDisabled();

        if (!target->landscape.heightmap_path.empty())
            ImGui::TextDisabled("Current: %s", target->landscape.heightmap_path.c_str());
        if (!m_heightmap_status.empty())
            TextDisabledWrapped(m_heightmap_status.c_str());

        ImGui::PopID();
    }

    ImGui::Separator();

    if (!m_brush)
    {
        ImGui::End();
        return;
    }

    // --- Tool palette ----------------------------------------------------
    ImGui::TextDisabled("Tool");
    const char *tool_names[] = { "Raise", "Smooth", "Flatten", "Paint" };
    const char *tool_hints[] = {
        "Lift or lower the surface under the brush.",
        "Blur heights toward the local neighborhood average.",
        "Pull heights toward the height under the brush center.",
        "Apply vertex color (material) to the terrain surface.",
    };
    const int current_tool = (int)m_brush->tool;
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::RadioButton(tool_names[i], current_tool == i))
            m_brush->tool = (SculptTool)i;
    }
    if (current_tool >= 0 && current_tool < 4)
        ImGui::TextDisabled("%s", tool_hints[current_tool]);

    // Material picker: only visible in Paint mode.
    if (current_tool == (int)SculptTool::Paint)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Material");
        struct MaterialPreset { const char *name; float r, g, b; };
        const MaterialPreset presets[] = {
            { "Grass",  0.30f, 0.55f, 0.20f },
            { "Rock",   0.45f, 0.42f, 0.38f },
            { "Dirt",   0.40f, 0.28f, 0.15f },
            { "Snow",   0.90f, 0.92f, 0.95f },
            { "Sand",   0.76f, 0.70f, 0.50f },
        };
        for (const auto &p : presets)
        {
            if (ImGui::ColorButton(p.name, ImVec4(p.r, p.g, p.b, 1.0f),
                                   0, ImVec2(20, 20)))
            {
                m_brush->paint_color[0] = p.r;
                m_brush->paint_color[1] = p.g;
                m_brush->paint_color[2] = p.b;
            }
            ImGui::SameLine();
            ImGui::Text("%s", p.name);
        }
        ImGui::ColorEdit3("Custom", m_brush->paint_color,
                          ImGuiColorEditFlags_NoInputs);
    }

    ImGui::Spacing();

    // --- Brush controls --------------------------------------------------
    ImGui::TextDisabled("Brush");
    ImGui::SliderFloat("Size", &m_brush->radius, 0.5f, 20.0f, "%.1f");
    ImGui::SliderFloat("Strength", &m_brush->strength, 0.01f, 2.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &m_brush->falloff, 0.0f, 1.0f, "%.2f");

    ImGui::Separator();
    if (target)
    {
        ImGui::TextWrapped(
            "Terrain '%s': %d x %d vertices over %.0f m.",
            target->tag.tag.c_str(),
            target->landscape.resolution + 1,
            target->landscape.resolution + 1,
            target->landscape.size);
        ImGui::Spacing();
        ImGui::TextDisabled("Hold LMB over the viewport and drag to paint.\n"
                            "Stroke is undoable (Edit > Undo).");
    }

    ImGui::End();
}
