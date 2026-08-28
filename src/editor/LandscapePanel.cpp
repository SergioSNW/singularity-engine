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
                               std::function<void()> on_create_landscape,
                               bool *paint_mode, int *paint_material_index,
                               bool *placement_mode)
    : m_scene(scene)
    , m_selection(selection)
    , m_brush(brush)
    , m_on_create_landscape(std::move(on_create_landscape))
    , m_paint_mode(paint_mode)
    , m_paint_material_index(paint_material_index)
    , m_placement_mode(placement_mode)
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

    // --- Mode: Sculpt vs Paint ---------------------------------------------
    // Two distinct brushes share this panel and the sliders below: Sculpt
    // edits height on the targeted terrain above (Raise/Lower/Flatten/
    // Smooth); Paint edits material and, unlike Sculpt, isn't limited to the
    // targeted terrain -- it reaches whatever landscape or placed primitive
    // (Wall/Floor/Ramp/...) the cursor is over. *m_paint_mode is the exact
    // flag the viewport toolbar's own Paint button reads and writes, so this
    // switch and that button always agree on whether painting is active.
    const bool has_paint_state = m_paint_mode && m_paint_material_index;
    const bool paint_mode_active = has_paint_state && *m_paint_mode;
    if (has_paint_state)
    {
        ImGui::SeparatorText("Mode");
        if (!paint_mode_active)
            Theme::PushPrimaryButtonColor();
        if (ImGui::Button("Sculpt", ImVec2(100.0f, 0.0f)))
            *m_paint_mode = false;
        if (!paint_mode_active)
            Theme::PopPrimaryButtonColor();
        ImGui::SameLine();
        if (paint_mode_active)
            Theme::PushPrimaryButtonColor();
        if (ImGui::Button("Paint", ImVec2(100.0f, 0.0f)))
        {
            *m_paint_mode = true;
            if (m_placement_mode)
                *m_placement_mode = false;
        }
        if (paint_mode_active)
            Theme::PopPrimaryButtonColor();
        ImGui::Spacing();
    }

    if (!paint_mode_active)
    {
        // --- Sculpt brush ---------------------------------------------------
        ImGui::SeparatorText("Sculpt Brush");
        const char *tool_names[] = { "Raise", "Lower", "Flatten", "Smooth" };
        const char *tool_hints[] = {
            "Lift the surface under the brush.",
            "Push the surface down under the brush.",
            "Pull heights toward the height under the brush center.",
            "Blur heights toward the local neighborhood average.",
        };
        const SculptTool sculpt_tools[] = {
            SculptTool::Raise, SculptTool::Lower, SculptTool::Flatten, SculptTool::Smooth,
        };
        int current_tool = 0;
        for (int i = 0; i < 4; ++i)
            if (m_brush->tool == sculpt_tools[i])
                current_tool = i;
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            if (ImGui::RadioButton(tool_names[i], current_tool == i))
                m_brush->tool = sculpt_tools[i];
        }
        ImGui::TextDisabled("%s", tool_hints[current_tool]);
    }
    else
    {
        // --- Paint material ---------------------------------------------
        ImGui::SeparatorText("Material");
        const int current_mat = std::clamp(*m_paint_material_index, 0,
                                           kLandscapePaintPaletteCount - 1);
        for (int i = 0; i < kLandscapePaintPaletteCount; ++i)
        {
            const PaintMaterialPreset &p = kLandscapePaintPalette[i];
            ImGui::PushID(i);
            const ImVec4 swatch(p.color[0], p.color[1], p.color[2], 1.0f);
            ImGui::ColorButton("##swatch", swatch,
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                               ImVec2(22.0f, 22.0f));
            ImGui::SameLine();
            if (ImGui::Selectable(p.name, current_mat == i, 0, ImVec2(0.0f, 22.0f)))
                *m_paint_material_index = i;
            ImGui::PopID();
        }
        ImGui::TextDisabled("Applies to terrain (blended) or a single placed "
                            "primitive (swapped outright).");
    }

    ImGui::Spacing();

    // --- Brush shape (shared by both modes) -------------------------------
    ImGui::SeparatorText("Brush");
    ImGui::SliderFloat("Size", &m_brush->radius, 0.5f, 20.0f, "%.1f");
    ImGui::SliderFloat("Strength", &m_brush->strength, 0.01f, 2.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &m_brush->falloff, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Edge:");
    ImGui::SameLine();
    bool sharp_edge = (m_brush->falloff_profile == BrushFalloffProfile::Sharp);
    if (ImGui::RadioButton("Smooth", !sharp_edge))
        m_brush->falloff_profile = BrushFalloffProfile::Smooth;
    ImGui::SameLine();
    if (ImGui::RadioButton("Sharp", sharp_edge))
        m_brush->falloff_profile = BrushFalloffProfile::Sharp;
    ImGui::TextDisabled("Smooth eases into the surrounding surface; Sharp "
                        "cuts a harder, more defined edge.");

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
        if (paint_mode_active)
            ImGui::TextDisabled("Hold LMB over the viewport to paint. "
                                "Stroke is undoable (Edit > Undo).");
        else
            ImGui::TextDisabled("Hold LMB over the viewport and drag to sculpt "
                                "'%s'. Stroke is undoable (Edit > Undo).",
                                target->tag.tag.c_str());
    }

    ImGui::End();
}
