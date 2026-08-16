#include "LandscapePanel.h"

#include "core/Entity.h"
#include "core/Landscape.h"
#include "core/Scene.h"
#include "SelectionState.h"

#include <utility>

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

    if (!m_brush)
    {
        ImGui::End();
        return;
    }

    // --- Tool palette ----------------------------------------------------
    ImGui::TextDisabled("Tool");
    const char *tool_names[] = { "Raise", "Smooth", "Flatten" };
    const char *tool_hints[] = {
        "Lift or lower the surface under the brush.",
        "Blur heights toward the local neighborhood average.",
        "Pull heights toward the height under the brush center.",
    };
    const int current_tool = (int)m_brush->tool;
    for (int i = 0; i < 3; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::RadioButton(tool_names[i], current_tool == i))
            m_brush->tool = (SculptTool)i;
    }
    if (current_tool >= 0 && current_tool < 3)
        ImGui::TextDisabled("%s", tool_hints[current_tool]);

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
