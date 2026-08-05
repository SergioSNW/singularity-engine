#include "InspectorPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"
#include "EngineMath.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

namespace {

// Discover loadable .obj assets under assets/meshes/ (sorted for stable UI).
std::vector<std::string> ListMeshAssets()
{
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator("assets/meshes", ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string path = entry.path().filename().string();
        if (path.size() > 4 && path.substr(path.size() - 4) == ".obj")
            out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Collapsible component header with consistent spacing and a subtle action
// button on the right edge of the header row. The button is a "ghost" — fully
// transparent until hovered — so headers stay quiet while the action stays
// one click away. Returns whether the body should be drawn.
bool ComponentHeader(const char *title, const char *action_label = nullptr,
                     std::function<void()> on_action = {}, bool default_open = true)
{
    ImGui::PushID(title);
    const bool open = ImGui::CollapsingHeader(
        title, default_open ? ImGuiTreeNodeFlags_DefaultOpen
                            : ImGuiTreeNodeFlags_None);
    if (open && action_label)
    {
        const float btn_w = ImGui::CalcTextSize(action_label).x
            + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btn_w);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
        if (ImGui::SmallButton(action_label) && on_action)
            on_action();
        ImGui::PopStyleColor(3);
    }
    ImGui::PopID();
    return open;
}

} // namespace

InspectorPanel::InspectorPanel(SelectionState *selection, Scene *scene)
    : m_selection(selection)
    , m_scene(scene)
{
}

void InspectorPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

    Entity *entity = (m_selection->entity_id >= 0)
        ? m_scene->GetEntityById(m_selection->entity_id)
        : nullptr;

    // Clean placeholder state: nothing to inspect, so no cluttered controls —
    // just a centered hint inviting a selection.
    if (!entity)
    {
        const float avail_y = ImGui::GetContentRegionAvail().y;
        const float avail_x = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0.0f, avail_y * 0.30f));

        const char *title = "No Entity Selected";
        const float tw = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_x - tw) * 0.5f);
        ImGui::TextDisabled("%s", title);

        const char *hint = "Select an entity in the Hierarchy to edit its components.";
        const float hw = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_x - hw) * 0.5f);
        ImGui::TextDisabled("%s", hint);

        ImGui::End();
        return;
    }

    if (m_selection->entity_id != m_last_selected_id)
    {
        m_last_selected_id = m_selection->entity_id;
        std::strncpy(m_tag_buffer, entity->tag.tag.c_str(), sizeof(m_tag_buffer) - 1);
        m_tag_buffer[sizeof(m_tag_buffer) - 1] = '\0';
        std::strncpy(m_mesh_buffer, entity->mesh.path.c_str(), sizeof(m_mesh_buffer) - 1);
        m_mesh_buffer[sizeof(m_mesh_buffer) - 1] = '\0';
        std::strncpy(m_script_buffer, entity->script.path.c_str(), sizeof(m_script_buffer) - 1);
        m_script_buffer[sizeof(m_script_buffer) - 1] = '\0';
    }

    // --- Identity header: rename the tag, show the stable id ---
    if (ImGui::InputText("Tag", m_tag_buffer, sizeof(m_tag_buffer)))
        entity->tag.tag = m_tag_buffer;
    ImGui::TextDisabled("Entity id %d", entity->id);

    ImGui::Spacing();
    ImGui::Separator();

    // --- Transform ---
    if (ComponentHeader("Transform", "Reset", [entity]() {
        std::fill(std::begin(entity->transform.position), std::end(entity->transform.position), 0.0f);
        std::fill(std::begin(entity->transform.rotation), std::end(entity->transform.rotation), 0.0f);
        entity->transform.scale[0] = 1.0f;
        entity->transform.scale[1] = 1.0f;
        entity->transform.scale[2] = 1.0f;
    }))
    {
        ImGui::DragFloat3("Position", entity->transform.position, 0.1f);
        ImGui::DragFloat3("Rotation", entity->transform.rotation, 0.1f);
        ImGui::DragFloat3("Scale",    entity->transform.scale,    0.1f);

        // Read-only world position: local transform folded through the parent
        // chain (WorldMatrix = ParentWorld * LocalMatrix), so a child reports
        // its actual location in scene space.
        Mat4 world = m_scene->ComputeWorldMatrix(*entity);
        ImGui::TextDisabled("World Position: (%.2f, %.2f, %.2f)",
                            world.m[12], world.m[13], world.m[14]);
    }

    // --- Hierarchy ---
    if (ComponentHeader("Hierarchy"))
    {
        // Parent combo: list every entity that is not this one or a descendant
        // (a reparent into its own subtree is a cycle and is rejected).
        if (ImGui::BeginCombo("Parent",
                              entity->parent ? entity->parent->tag.tag.c_str() : "None"))
        {
            if (ImGui::Selectable("None", entity->parent == nullptr))
                m_scene->SetParent(entity->id, -1);

            for (auto &candidate : m_scene->GetEntities())
            {
                if (candidate->id == entity->id ||
                    m_scene->IsDescendantOf(candidate->id, entity->id))
                    continue;
                bool is_current = entity->parent == candidate.get();
                if (ImGui::Selectable(candidate->tag.tag.c_str(), is_current))
                    m_scene->SetParent(entity->id, candidate->id);
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("Children: %d", (int)entity->children.size());
    }

    // --- Material ---
    if (ComponentHeader("Material", "Reset", [entity]() {
        entity->material.color[0] = 1.0f;
        entity->material.color[1] = 1.0f;
        entity->material.color[2] = 1.0f;
        entity->material.color[3] = 1.0f;
        entity->material.active = true;
    }))
    {
        ImGui::ColorEdit4("Albedo", entity->material.color);
        ImGui::Checkbox("Active", &entity->material.active);
    }

    // --- Mesh ---
    if (ComponentHeader("Mesh", "Reset to Cube", [this, entity]() {
        entity->mesh.path.clear();
        m_mesh_buffer[0] = '\0';
    }))
    {
        const char *preview = entity->mesh.path.empty()
            ? "Cube Primitive"
            : entity->mesh.path.c_str();
        if (ImGui::BeginCombo("Asset", preview))
        {
            if (ImGui::Selectable("Cube Primitive", entity->mesh.path.empty()))
                entity->mesh.path.clear();
            for (const std::string &path : ListMeshAssets())
            {
                bool selected = (entity->mesh.path == path);
                if (ImGui::Selectable(path.c_str(), selected))
                {
                    entity->mesh.path = path;
                    std::strncpy(m_mesh_buffer, path.c_str(), sizeof(m_mesh_buffer) - 1);
                    m_mesh_buffer[sizeof(m_mesh_buffer) - 1] = '\0';
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::InputText("Path", m_mesh_buffer, sizeof(m_mesh_buffer)))
        {
            if (m_mesh_buffer[0] == '\0')
                entity->mesh.path.clear();
        }
        if (ImGui::Button("Apply Path"))
            entity->mesh.path = m_mesh_buffer;
        ImGui::SameLine();
        if (ImGui::Button("Reset to Cube"))
        {
            entity->mesh.path.clear();
            m_mesh_buffer[0] = '\0';
        }
        ImGui::TextDisabled("OBJ assets under assets/meshes/; empty = cube primitive");
    }

    // --- Collider ---
    if (ComponentHeader("Collider", "Reset", [entity]() {
        entity->collider.enabled = false;
        entity->collider.type = ColliderComponent::Type::Solid;
        entity->collider.center = { 0.0f, 0.0f, 0.0f };
        entity->collider.extents = { 0.5f, 0.5f, 0.5f };
    }))
    {
        ImGui::Checkbox("Enabled", &entity->collider.enabled);
        const char *preview = (entity->collider.type == ColliderComponent::Type::Trigger)
            ? "Trigger" : "Solid";
        if (ImGui::BeginCombo("Type", preview))
        {
            if (ImGui::Selectable("Solid", entity->collider.type == ColliderComponent::Type::Solid))
                entity->collider.type = ColliderComponent::Type::Solid;
            if (ImGui::Selectable("Trigger", entity->collider.type == ColliderComponent::Type::Trigger))
                entity->collider.type = ColliderComponent::Type::Trigger;
            ImGui::EndCombo();
        }
        ImGui::DragFloat3("Center", &entity->collider.center.x, 0.1f);
        ImGui::DragFloat3("Extents", &entity->collider.extents.x, 0.05f, 0.01f, 100.0f);
        ImGui::TextDisabled("Solid blocks solids; Trigger is pass-through (events only)");
    }

    // --- Script ---
    if (ComponentHeader("Script", "Clear", [this, entity]() {
        entity->script.path.clear();
        m_script_buffer[0] = '\0';
    }))
    {
        // Text input writes the buffer live (like the mesh path field); Apply
        // commits it, and empty means no script. Scripts bind when the scene
        // enters play mode.
        ImGui::InputText("Path", m_script_buffer, sizeof(m_script_buffer));
        if (ImGui::Button("Apply Script"))
        {
            if (m_script_buffer[0] == '\0')
                entity->script.path.clear();
            else
                entity->script.path = m_script_buffer;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            entity->script.path.clear();
            m_script_buffer[0] = '\0';
        }
        ImGui::TextDisabled("Lua file under assets/scripts/; empty = no script");
    }

    // --- Camera ---
    if (ComponentHeader("Camera", "Reset", [entity]() {
        entity->camera.fov = 60.0f;
        entity->camera.near_plane = 0.1f;
        entity->camera.far_plane = 100.0f;
        entity->camera.pitch = 0.0f;
        entity->camera.yaw = 0.0f;
        entity->camera.primary = false;
    }))
    {
        ImGui::DragFloat("FOV", &entity->camera.fov, 0.5f, 1.0f, 179.0f);
        ImGui::DragFloat("Pitch", &entity->camera.pitch, 0.1f, -89.0f, 89.0f);
        ImGui::DragFloat("Yaw",   &entity->camera.yaw,   0.1f);
        ImGui::DragFloat("Near", &entity->camera.near_plane, 0.01f, 0.001f, 10.0f);
        ImGui::DragFloat("Far",  &entity->camera.far_plane,  0.1f,  0.1f, 1000.0f);
        ImGui::Checkbox("Primary", &entity->camera.primary);
    }

    ImGui::End();
}
