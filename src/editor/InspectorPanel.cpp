#include "InspectorPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"
#include "EngineMath.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
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

} // namespace

InspectorPanel::InspectorPanel(SelectionState *selection, Scene *scene)
    : m_selection(selection)
    , m_scene(scene)
{
}

void InspectorPanel::OnImGuiRender(float dt)
{
    (void)dt;

    ImGui::Begin("Inspector", nullptr,
        ImGuiWindowFlags_NoCollapse);

    if (m_selection->entity_id < 0)
    {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    Entity *entity = m_scene->GetEntityById(m_selection->entity_id);
    if (!entity)
    {
        ImGui::TextDisabled("Entity not found");
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

    if (ImGui::InputText("Tag", m_tag_buffer, sizeof(m_tag_buffer)))
        entity->tag.tag = m_tag_buffer;

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("Albedo", entity->material.color);
        ImGui::Checkbox("Active", &entity->material.active);
    }

    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
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
