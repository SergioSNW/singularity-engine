#include "TimelinePanel.h"

#include "Entity.h"
#include "Scene.h"
#include "SelectionState.h"

#include <algorithm>
#include <cmath>

TimelinePanel::TimelinePanel(Scene *scene, SelectionState *selection,
                             TimelineBridge *bridge)
    : m_scene(scene)
    , m_selection(selection)
    , m_bridge(bridge)
{
}

void TimelinePanel::OnImGuiRender(float dt)
{
    (void)dt;
    if (!m_visible)
        return;
    if (!m_bridge || !m_bridge->state)
        return;

    ImGui::Begin("Timeline");

    DrawTransport();

    Entity *entity = m_scene->GetEntityById(m_selection->entity_id);
    if (!entity)
    {
        ImGui::TextDisabled("Select an entity to edit its keyframes.");
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "%s", entity->tag.tag.c_str());
    ImGui::Separator();

    DrawLane("Position", entity->animation.position, AnimProperty::Position);
    DrawLane("Rotation", entity->animation.rotation, AnimProperty::Rotation);
    DrawLane("Scale",    entity->animation.scale,    AnimProperty::Scale);

    ImGui::End();
}

void TimelinePanel::DrawTransport()
{
    TimelineState &state = *m_bridge->state;

    if (m_bridge->on_play_pause)
    {
        if (ImGui::Button(state.playing ? "Pause" : "Play"))
            m_bridge->on_play_pause();
        ImGui::SameLine();
    }
    if (m_bridge->on_stop)
    {
        if (ImGui::Button("Stop"))
            m_bridge->on_stop();
        ImGui::SameLine();
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    float t = state.time;
    if (ImGui::SliderFloat("##time", &t, 0.0f, state.duration, "%.2f s"))
        ScrubTo(t);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    float duration = state.duration;
    if (ImGui::DragFloat("Duration", &duration, 0.1f, 0.1f, 600.0f))
        state.duration = std::max(0.1f, duration);

    ImGui::SameLine();
    ImGui::Checkbox("Loop", &state.loop);

    ImGui::Separator();
}

// One keyframe lane: label + key count + record button on the left, the track
// strip on the right. Clicking the strip scrubs the playhead; right-clicking a
// keyframe diamond removes it.
void TimelinePanel::DrawLane(const char *label, const AnimationTrack &track,
                             AnimProperty prop)
{
    const float row_h = 30.0f;
    const float lane_h = row_h - 8.0f;
    ImGui::TextUnformatted(label);
    ImGui::SameLine(120.0f);
    ImGui::TextDisabled("%d key%s", (int)track.Size(), track.Size() == 1 ? "" : "s");
    ImGui::SameLine();

    if (m_bridge->on_set_keyframe)
    {
        if (ImGui::SmallButton("+"))
            m_bridge->on_set_keyframe(prop);
        ImGui::SameLine();
    }

    const float lane_x = ImGui::GetCursorScreenPos().x;
    const float lane_y = ImGui::GetCursorScreenPos().y;
    const float lane_w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float lane_bottom = lane_y + lane_h;
    const float duration = std::max(0.1f, m_bridge->state->duration);
    const float mid_y = lane_y + lane_h * 0.5f;

    ImGui::InvisibleButton("lane", ImVec2(lane_w, lane_h));
    const bool lane_hovered = ImGui::IsItemHovered();
    const bool lane_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool lane_rclicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(lane_x, lane_y), ImVec2(lane_x + lane_w, lane_bottom),
                      IM_COL32(28, 28, 36, 255));
    dl->AddRect(ImVec2(lane_x, lane_y), ImVec2(lane_x + lane_w, lane_bottom),
                IM_COL32(70, 70, 84, 255));

    // Snapshot the key vector to avoid iterator invalidation if a keyframe is
    // added/removed via a callback during the same frame.
    const std::vector<AnimationKeyframe> keys_snapshot = track.keys;

    // Keyframe diamonds.
    for (const AnimationKeyframe &k : keys_snapshot)
    {
        const float kx = lane_x + (k.time / duration) * lane_w;
        dl->AddQuadFilled(ImVec2(kx, mid_y - 4.0f), ImVec2(kx + 4.0f, mid_y),
                          ImVec2(kx, mid_y + 4.0f), ImVec2(kx - 4.0f, mid_y),
                          IM_COL32(205, 120, 235, 255));
    }

    // Playhead.
    const float ph_x = lane_x + (m_bridge->state->time / duration) * lane_w;
    dl->AddLine(ImVec2(ph_x, lane_y), ImVec2(ph_x, lane_bottom),
                IM_COL32(255, 200, 70, 255), 1.0f);

    if (lane_hovered)
    {
        const float mouse_t = (ImGui::GetIO().MousePos.x - lane_x) / lane_w * duration;
        dl->AddLine(ImVec2(ImGui::GetIO().MousePos.x, lane_y),
                    ImVec2(ImGui::GetIO().MousePos.x, lane_bottom),
                    IM_COL32(160, 160, 180, 90), 1.0f);
        if (lane_clicked)
            ScrubTo(std::max(0.0f, std::min(duration, mouse_t)));
        else if (lane_rclicked && m_bridge->on_remove_keyframe)
        {
            for (const AnimationKeyframe &k : keys_snapshot)
            {
                const float kx = lane_x + (k.time / duration) * lane_w;
                if (std::fabs(ImGui::GetIO().MousePos.x - kx) < 6.0f)
                {
                    m_bridge->on_remove_keyframe(prop, k.time);
                    break;
                }
            }
        }
    }
}

void TimelinePanel::ScrubTo(float time)
{
    if (!m_bridge->state || !m_bridge->on_scrub)
        return;
    m_bridge->state->time = std::max(0.0f, time);
    m_bridge->on_scrub();
}
