#include "ProfilerPanel.h"

#include "core/Profiler.h"

#include <imgui.h>
#include <cstdio>
#include <limits>

ProfilerPanel::ProfilerPanel(Profiler *profiler)
    : m_profiler(profiler)
{
}

// Draw one rolling series as an ImGui line plot. `scale` converts samples to
// the unit displayed (e.g. seconds -> milliseconds); returns the text the
// caller appends to the label row.
static void PlotSeries(const char *label, const Profiler::Series &series,
                       float scale, ImVec2 size)
{
    if (series.Count() == 0)
    {
        ImGui::PlotLines(label, (const float *)nullptr, 0, 0,
                         nullptr, FLT_MAX, FLT_MAX, size);
        return;
    }

    float values[Profiler::kRingSize];
    for (size_t i = 0; i < series.Count(); ++i)
        values[i] = series.At(i) * scale;

    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.1f", series.Latest() * scale);
    ImGui::PlotLines(label, values, (int)series.Count(), 0, overlay,
                     FLT_MAX, FLT_MAX, size);
}

void ProfilerPanel::OnImGuiRender(float dt)
{
    (void)dt;

    if (!m_visible)
        return;

    if (!ImGui::Begin("Profiler", &m_visible, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    if (!m_profiler)
    {
        ImGui::TextDisabled("No profiler available.");
        ImGui::End();
        return;
    }

    const bool paused = m_profiler->IsPaused();
    if (ImGui::Button(paused ? "Resume" : "Pause"))
        m_profiler->SetPaused(!paused);
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        m_profiler->Clear();
    ImGui::SameLine();
    if (paused)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "FROZEN");
    ImGui::SameLine();
    ImGui::TextDisabled("%d frames recorded", m_profiler->FrameCount());

    ImGui::Spacing();
    ImGui::Separator();

    // Frame time + per-stage timing (milliseconds). Latest / avg / peak for
    // the row, then a 120-frame trend plot underneath.
    const ImVec2 plot_size(0.0f, 48.0f);

    ImGui::TextUnformatted("Frame timing (ms)");
    ImGui::Separator();

    PlotSeries("Frame", m_profiler->Total(), 1000.0f, plot_size);

    for (int s = 0; s < Profiler::StageCount; ++s)
    {
        const Profiler::Stage stage = (Profiler::Stage)s;
        const Profiler::Series &series = m_profiler->StageSeries(stage);
        const char *name = Profiler::StageName(stage);

        ImGui::Text("%s  latest %.2f  avg %.2f  peak %.2f", name,
                    series.Latest() * 1000.0f,
                    series.Average() * 1000.0f,
                    series.Max() * 1000.0f);
        PlotSeries(name, series, 1000.0f, plot_size);
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Resource snapshot (latest values + trends).
    ImGui::TextUnformatted("Resources");
    ImGui::Separator();

    ImGui::Text("Entities  %d", m_profiler->Entities());
    PlotSeries("Entities", m_profiler->EntitiesSeries(), 1.0f, plot_size);

    ImGui::Text("3D draw calls  %d", m_profiler->DrawCalls());
    PlotSeries("Draw calls", m_profiler->DrawCallsSeries(), 1.0f, plot_size);

    const double mb = (double)m_profiler->MemoryBytes() / (1024.0 * 1024.0);
    ImGui::Text("Resident memory  %.2f MB", mb);
    PlotSeries("Memory MB", m_profiler->MemorySeries(), 1.0f / (1024.0f * 1024.0f), plot_size);

    ImGui::End();
}
