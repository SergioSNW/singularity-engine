#include "InspectorPanel.h"
#include "SelectionState.h"
#include "Scene.h"
#include "Entity.h"
#include "EngineMath.h"
#include "Material.h"
#include "Texture.h"
#include "AudioManager.h"
#include "core/CommandHistory.h"

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

// .mat material assets under assets/materials/.
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

// Drag payload type used by the Content Browser when a .mat / image asset is
// dragged. Kept as string literals (same values in both panels).
constexpr const char *kMaterialPayload = "MATERIAL";
constexpr const char *kTexturePayload = "TEXTURE";

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

InspectorPanel::InspectorPanel(SelectionState *selection, Scene *scene,
                               MaterialLibrary *material_library,
                               TextureLibrary *texture_library,
                               CommandHistory *history, AudioManager *audio)
    : m_selection(selection)
    , m_scene(scene)
    , m_material_library(material_library)
    , m_texture_library(texture_library)
    , m_history(history)
    , m_audio(audio)
{
}

void InspectorPanel::BeginEditSession(const char *description)
{
    if (!m_history || m_selection->entity_id < 0)
        return;
    if (m_edit_entity == m_selection->entity_id)
        return;  // one open session covers the whole entity
    if (m_edit_entity >= 0)
        m_history->EndEntityEdit();  // commit / discard the stale session
    m_history->BeginEntityEdit(m_selection->entity_id, description);
    m_edit_entity = m_selection->entity_id;
}

void InspectorPanel::EndEditSessionIfReleased()
{
    if (!m_history || m_edit_entity < 0)
        return;
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        m_history->EndEntityEdit();
        m_edit_entity = -1;
    }
}

void InspectorPanel::CommitEdit(const char *description,
                                const std::function<void()> &mutate)
{
    if (!m_history)
    {
        mutate();
        return;
    }
    m_history->BeginEntityEdit(m_selection->entity_id, description);
    mutate();
    m_history->EndEntityEdit();
    m_edit_entity = -1;  // resync: CommitEdit owned the transaction
}

void InspectorPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    ImGui::Begin("Inspector", &m_visible, ImGuiWindowFlags_NoCollapse);

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
        std::strncpy(m_audio_buffer, entity->audio.path.c_str(), sizeof(m_audio_buffer) - 1);
        m_audio_buffer[sizeof(m_audio_buffer) - 1] = '\0';
    }

    // --- Identity header: rename the tag, show the stable id ---
    BeginEditSession("Rename Tag");
    if (ImGui::InputText("Tag", m_tag_buffer, sizeof(m_tag_buffer)))
        entity->tag.tag = m_tag_buffer;
    EndEditSessionIfReleased();
    ImGui::TextDisabled("Entity id %d", entity->id);

    ImGui::Spacing();
    ImGui::Separator();

    // --- Transform ---
    if (ComponentHeader("Transform", "Reset", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Reset Transform");
        std::fill(std::begin(entity->transform.position), std::end(entity->transform.position), 0.0f);
        std::fill(std::begin(entity->transform.rotation), std::end(entity->transform.rotation), 0.0f);
        entity->transform.scale[0] = 1.0f;
        entity->transform.scale[1] = 1.0f;
        entity->transform.scale[2] = 1.0f;
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;  // resync: the reset owned the transaction
        }
    }))
    {
        BeginEditSession("Edit Transform");
        ImGui::DragFloat3("Position", entity->transform.position, 0.1f);
        EndEditSessionIfReleased();
        ImGui::DragFloat3("Rotation", entity->transform.rotation, 0.1f);
        EndEditSessionIfReleased();
        ImGui::DragFloat3("Scale",    entity->transform.scale,    0.1f);
        EndEditSessionIfReleased();

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
                CommitEdit("Re-parent", [this, entity]() {
                    m_scene->SetParent(entity->id, -1);
                });

            for (auto &candidate : m_scene->GetEntities())
            {
                if (candidate->id == entity->id ||
                    m_scene->IsDescendantOf(candidate->id, entity->id))
                    continue;
                bool is_current = entity->parent == candidate.get();
                if (ImGui::Selectable(candidate->tag.tag.c_str(), is_current))
                    CommitEdit("Re-parent", [this, entity, &candidate]() {
                        m_scene->SetParent(entity->id, candidate->id);
                    });
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("Children: %d", (int)entity->children.size());
    }

    // --- Material ---
    if (ComponentHeader("Material", "Reset", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Reset Material");
        entity->material.color[0] = 1.0f;
        entity->material.color[1] = 1.0f;
        entity->material.color[2] = 1.0f;
        entity->material.color[3] = 1.0f;
        entity->material.active = true;
        entity->material.material_path.clear();
        entity->material.texture_path.clear();
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        BeginEditSession("Edit Material");
        ImGui::ColorEdit4("Albedo", entity->material.color);
        EndEditSessionIfReleased();
        ImGui::Checkbox("Active", &entity->material.active);
        EndEditSessionIfReleased();

        // Resolve the effective texture/tint so the UI reflects what renders:
        // an assigned .mat asset wins, otherwise the direct texture_path.
        const Material *mat = nullptr;
        if (!entity->material.material_path.empty() && m_material_library)
            mat = m_material_library->Load(entity->material.material_path);
        std::string tex_key = entity->material.texture_path;
        if (mat && !mat->texture.empty())
            tex_key = mat->texture;

        ImGui::Spacing();
        ImGui::Separator();

        // --- Material asset (.mat) ---
        const char *mat_preview = entity->material.material_path.empty()
            ? "None" : entity->material.material_path.c_str();
        if (ImGui::BeginCombo("Material Asset", mat_preview))
        {
            if (ImGui::Selectable("None", entity->material.material_path.empty()))
                CommitEdit("Assign Material", [this, entity]() {
                    entity->material.material_path.clear();
                });
            for (const std::string &path : ListMaterialAssets())
            {
                bool selected = (entity->material.material_path == path);
                if (ImGui::Selectable(path.c_str(), selected))
                    CommitEdit("Assign Material", [this, entity, path]() {
                        entity->material.material_path = path;
                    });
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("A .mat asset sets tint, texture and shininess");

        if (ImGui::Button("New Material"))
            m_new_material_open = !m_new_material_open;
        ImGui::SameLine();
        ImGui::TextDisabled("Create a .mat asset from the current albedo");
        if (m_new_material_open)
        {
            ImGui::InputText("File Name", m_new_material_buffer, sizeof(m_new_material_buffer));
            if (ImGui::Button("Create"))
            {
                std::string name = m_new_material_buffer;
                if (!name.empty())
                {
                    if (name.size() < 4 || name.substr(name.size() - 4) != ".mat")
                        name += ".mat";
                    Material new_mat;
                    new_mat.color[0] = entity->material.color[0];
                    new_mat.color[1] = entity->material.color[1];
                    new_mat.color[2] = entity->material.color[2];
                    new_mat.color[3] = entity->material.color[3];
                    std::string error;
                    if (m_material_library &&
                        m_material_library->Create(name, new_mat, &error))
                        CommitEdit("Assign Material", [this, entity, name]() {
                            entity->material.material_path = name;
                        });
                    m_new_material_open = false;
                    m_new_material_buffer[0] = '\0';
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                m_new_material_open = false;
                m_new_material_buffer[0] = '\0';
            }
        }

        // --- Texture map ---
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::BeginCombo("Texture", tex_key.empty() ? "None" : tex_key.c_str()))
        {
            if (ImGui::Selectable("None", tex_key.empty()))
                CommitEdit("Assign Texture", [this, entity]() {
                    entity->material.texture_path.clear();
                });
            for (const std::string &path : ListTextureAssets())
            {
                bool selected = (tex_key == path);
                if (ImGui::Selectable(path.c_str(), selected))
                    CommitEdit("Assign Texture", [this, entity, path]() {
                        entity->material.texture_path = path;
                    });
            }
            ImGui::EndCombo();
        }
        if (!tex_key.empty())
            ImGui::TextDisabled("Diffuse map from assets/textures/");
        else
            ImGui::TextDisabled("No texture: flat albedo shading");

        // Texture preview (ImGui's SDL renderer backend stores the SDL texture
        // handle directly in ImTextureID, so Image can render it in-panel).
        if (!tex_key.empty() && m_texture_library)
        {
            if (const TextureInfo *info = m_texture_library->Load(tex_key))
            {
                const float size = 96.0f;
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

        // Drop a .mat or image asset straight from the Content Browser.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kMaterialPayload))
            {
                const char *data = (const char *)payload->Data;
                if (data && *data)
                    CommitEdit("Assign Material", [this, entity, data]() {
                        entity->material.material_path = data;
                    });
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kTexturePayload))
            {
                const char *data = (const char *)payload->Data;
                if (data && *data)
                    CommitEdit("Assign Texture", [this, entity, data]() {
                        entity->material.texture_path = data;
                    });
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextDisabled("Drag a .mat / image from the Content Browser to assign");
    }

    // --- Mesh ---
    if (ComponentHeader("Mesh", "Reset to Cube", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Reset Mesh");
        entity->mesh.path.clear();
        m_mesh_buffer[0] = '\0';
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        const char *preview = entity->mesh.path.empty()
            ? "Cube Primitive"
            : entity->mesh.path.c_str();
        if (ImGui::BeginCombo("Asset", preview))
        {
            if (ImGui::Selectable("Cube Primitive", entity->mesh.path.empty()))
                CommitEdit("Change Mesh", [this, entity]() {
                    entity->mesh.path.clear();
                });
            for (const std::string &path : ListMeshAssets())
            {
                bool selected = (entity->mesh.path == path);
                if (ImGui::Selectable(path.c_str(), selected))
                    CommitEdit("Change Mesh", [this, entity, path]() {
                        entity->mesh.path = path;
                        std::strncpy(m_mesh_buffer, path.c_str(), sizeof(m_mesh_buffer) - 1);
                        m_mesh_buffer[sizeof(m_mesh_buffer) - 1] = '\0';
                    });
            }
            ImGui::EndCombo();
        }

        BeginEditSession("Change Mesh");
        if (ImGui::InputText("Path", m_mesh_buffer, sizeof(m_mesh_buffer)))
        {
            if (m_mesh_buffer[0] == '\0')
                entity->mesh.path.clear();
        }
        EndEditSessionIfReleased();
        if (ImGui::Button("Apply Path"))
            CommitEdit("Change Mesh", [this, entity]() {
                entity->mesh.path = m_mesh_buffer;
            });
        ImGui::SameLine();
        if (ImGui::Button("Reset to Cube"))
            CommitEdit("Change Mesh", [this, entity]() {
                entity->mesh.path.clear();
                m_mesh_buffer[0] = '\0';
            });
        ImGui::TextDisabled("OBJ assets under assets/meshes/; empty = cube primitive");
    }

    // --- Collider ---
    if (ComponentHeader("Collider", "Reset", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Reset Collider");
        entity->collider.enabled = false;
        entity->collider.type = ColliderComponent::Type::Solid;
        entity->collider.center = { 0.0f, 0.0f, 0.0f };
        entity->collider.extents = { 0.5f, 0.5f, 0.5f };
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        BeginEditSession("Edit Collider");
        ImGui::Checkbox("Enabled", &entity->collider.enabled);
        EndEditSessionIfReleased();
        const char *preview = (entity->collider.type == ColliderComponent::Type::Trigger)
            ? "Trigger" : "Solid";
        if (ImGui::BeginCombo("Type", preview))
        {
            if (ImGui::Selectable("Solid", entity->collider.type == ColliderComponent::Type::Solid))
                CommitEdit("Edit Collider", [this, entity]() {
                    entity->collider.type = ColliderComponent::Type::Solid;
                });
            if (ImGui::Selectable("Trigger", entity->collider.type == ColliderComponent::Type::Trigger))
                CommitEdit("Edit Collider", [this, entity]() {
                    entity->collider.type = ColliderComponent::Type::Trigger;
                });
            ImGui::EndCombo();
        }
        ImGui::DragFloat3("Center", &entity->collider.center.x, 0.1f);
        EndEditSessionIfReleased();
        ImGui::DragFloat3("Extents", &entity->collider.extents.x, 0.05f, 0.01f, 100.0f);
        EndEditSessionIfReleased();
        ImGui::TextDisabled("Solid blocks solids; Trigger is pass-through (events only)");
    }

    // --- Script ---
    if (ComponentHeader("Script", "Clear", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Clear Script");
        entity->script.path.clear();
        m_script_buffer[0] = '\0';
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        // Text input writes the buffer live (like the mesh path field); Apply
        // commits it, and empty means no script. Scripts bind when the scene
        // enters play mode.
        BeginEditSession("Change Script");
        ImGui::InputText("Path", m_script_buffer, sizeof(m_script_buffer));
        EndEditSessionIfReleased();
        if (ImGui::Button("Apply Script"))
            CommitEdit("Change Script", [this, entity]() {
                if (m_script_buffer[0] == '\0')
                    entity->script.path.clear();
                else
                    entity->script.path = m_script_buffer;
            });
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            CommitEdit("Change Script", [this, entity]() {
                entity->script.path.clear();
                m_script_buffer[0] = '\0';
            });
        ImGui::TextDisabled("Lua file under assets/scripts/; empty = no script");
    }

    // --- Audio ---
    if (ComponentHeader("Audio", "Clear", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Clear Audio");
        entity->audio = AudioComponent();
        m_audio_buffer[0] = '\0';
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        // Path text input (like the script/mesh fields); Apply commits it, and
        // empty means no audio. The path is a WAV/OGG asset under
        // assets/audio/, played on demand by the AudioManager.
        BeginEditSession("Change Audio");
        ImGui::InputText("Path", m_audio_buffer, sizeof(m_audio_buffer));
        EndEditSessionIfReleased();
        if (ImGui::Button("Apply Audio"))
            CommitEdit("Change Audio", [this, entity]() {
                if (m_audio_buffer[0] == '\0')
                    entity->audio.path.clear();
                else
                    entity->audio.path = m_audio_buffer;
            });
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            CommitEdit("Change Audio", [this, entity]() {
                entity->audio = AudioComponent();
                m_audio_buffer[0] = '\0';
            });

        ImGui::SliderFloat("Volume", &entity->audio.volume, 0.0f, 1.0f);
        EndEditSessionIfReleased();
        ImGui::Checkbox("Loop", &entity->audio.loop);
        EndEditSessionIfReleased();
        ImGui::Checkbox("Auto Play", &entity->audio.auto_play);
        EndEditSessionIfReleased();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("WAV/OGG under assets/audio/; empty = no audio");
        if (ImGui::Button("Preview Play") && !entity->audio.path.empty() && m_audio)
            m_audio->Play(entity->audio.path, entity->audio.volume,
                          entity->audio.loop);
        ImGui::SameLine();
        if (ImGui::Button("Preview Stop") && m_audio)
            m_audio->Stop(entity->audio.path);
    }

    // --- Camera ---
    if (ComponentHeader("Camera", "Reset", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Reset Camera");
        entity->camera.fov = 60.0f;
        entity->camera.near_plane = 0.1f;
        entity->camera.far_plane = 100.0f;
        entity->camera.pitch = 0.0f;
        entity->camera.yaw = 0.0f;
        entity->camera.primary = false;
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        BeginEditSession("Edit Camera");
        ImGui::DragFloat("FOV", &entity->camera.fov, 0.5f, 1.0f, 179.0f);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Pitch", &entity->camera.pitch, 0.1f, -89.0f, 89.0f);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Yaw",   &entity->camera.yaw,   0.1f);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Near", &entity->camera.near_plane, 0.01f, 0.001f, 10.0f);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Far",  &entity->camera.far_plane,  0.1f,  0.1f, 1000.0f);
        EndEditSessionIfReleased();
        ImGui::Checkbox("Primary", &entity->camera.primary);
        EndEditSessionIfReleased();
    }

    // --- Directional Light ---
    if (ComponentHeader("Directional Light", "Reset", [this, entity]() {
        if (m_history)
            m_history->BeginEntityEdit(entity->id, "Reset Light");
        entity->light = DirectionalLightComponent();
        entity->light.active = true;  // reset restores a lit light, like Material
        if (m_history)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }))
    {
        BeginEditSession("Edit Light");
        ImGui::Checkbox("Active", &entity->light.active);
        EndEditSessionIfReleased();
        ImGui::ColorEdit3("Color", entity->light.color);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Intensity", &entity->light.intensity, 0.05f, 0.0f, 10.0f);
        EndEditSessionIfReleased();
        ImGui::DragFloat3("Direction", entity->light.direction, 0.05f);
        EndEditSessionIfReleased();
        ImGui::SliderFloat("Ambient", &entity->light.ambient, 0.0f, 1.0f);
        EndEditSessionIfReleased();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Directional shadow attenuation");
        ImGui::SliderFloat("Shadow Strength", &entity->light.shadow_strength, 0.0f, 1.0f);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Shadow Bias", &entity->light.shadow_bias, 0.001f, 0.0f, 1.0f);
        EndEditSessionIfReleased();
        ImGui::DragFloat("Shadow Distance", &entity->light.shadow_distance, 0.5f, 0.0f, 500.0f);
        EndEditSessionIfReleased();
        ImGui::TextDisabled("Light travels along 'Direction'; faces toward the source are lit");
    }

    // If the panel loses the session target (selection cleared/changed without
    // a commit), close the dangling transaction now so the next selection
    // starts clean.
    if (m_edit_entity >= 0 && m_history)
    {
        if (m_selection->entity_id != m_edit_entity)
        {
            m_history->EndEntityEdit();
            m_edit_entity = -1;
        }
    }

    ImGui::End();
}
