#pragma once

// Real-time performance telemetry (Phase 30). A fixed-size rolling buffer of
// per-stage frame times (Update / Render / UI / Physics) plus a live resource
// snapshot (entity count, 3D draw calls, resident memory bytes). The engine
// drives it from Application::Run() with StartFrame/EndFrame around each
// stage; the ProfilerPanel renders the series as ImGui plots. Deliberately
// dependency-free (no SDL / ImGui) so it can be unit-tested standalone and
// stays cheap enough to run every frame on thermally-constrained hardware.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>

class Profiler
{
public:
    enum Stage : int
    {
        Update = 0,
        Render = 1,
        UI     = 2,
        Physics = 3,
        StageCount = 4,
    };

    // Rolling history length: one sample per frame, 120 frames of lookback.
    static constexpr size_t kRingSize = 120;

    // One rolling series: a circular buffer plus running sum/max so the UI can
    // read latest / average / peak without rescanning the window every frame.
    // `At(0)` is the oldest sample, `At(Count()-1)` the most recent.
    class Series
    {
    public:
        void Push(float v)
        {
            const float evicted = m_data[m_head];
            m_data[m_head] = v;
            m_head = (m_head + 1) % kRingSize;
            if (m_count < kRingSize)
            {
                m_sum += v;
                ++m_count;
            }
            else
            {
                m_sum += v - evicted;
            }
            m_max = std::max(m_max, v);
        }
        void Clear()
        {
            m_data.fill(0.0f);
            m_head = 0;
            m_count = 0;
            m_sum = 0.0f;
            m_max = 0.0f;
        }
        size_t Count() const { return m_count; }
        float At(size_t i) const
        {
            const size_t oldest = (m_head + kRingSize - m_count) % kRingSize;
            return m_data[(oldest + i) % kRingSize];
        }
        float Latest() const { return m_count ? At(m_count - 1) : 0.0f; }
        float Average() const { return m_count ? m_sum / (float)m_count : 0.0f; }
        float Max() const { return m_max; }

    private:
        std::array<float, kRingSize> m_data{};
        size_t m_head = 0;
        size_t m_count = 0;
        float m_sum = 0.0f;
        float m_max = 0.0f;
    };

    Profiler() = default;

    // Frame lifecycle. StartFrame() opens the total-frame clock and zeroes the
    // per-stage accumulators; EndFrame() commits one sample per stage plus the
    // total. Begin/EndStage bracket each phase and may be repeated (the
    // accumulator sums disjoint spans). While paused all of these are no-ops so
    // the rolling history freezes in place.
    void StartFrame();
    void EndFrame();
    void BeginStage(Stage s);
    void EndStage(Stage s);

    // Per-frame resource snapshot. The latest values are always readable (even
    // while paused); the series only advance while unpaused.
    void RecordResources(int entities, int draw_calls, size_t memory_bytes);

    void SetPaused(bool paused) { m_paused = paused; }
    bool IsPaused() const { return m_paused; }

    // Drops every sample and resets the resource snapshot.
    void Clear();

    const Series &StageSeries(Stage s) const { return m_stages[(size_t)s]; }
    const Series &Total() const { return m_total; }
    const Series &EntitiesSeries() const { return m_entities_series; }
    const Series &DrawCallsSeries() const { return m_draw_calls_series; }
    const Series &MemorySeries() const { return m_memory_series; }

    float Last(Stage s) const { return m_stages[(size_t)s].Latest(); }
    float TotalLast() const { return m_total.Latest(); }
    int FrameCount() const { return (int)m_total.Count(); }

    int Entities() const { return m_entities; }
    int DrawCalls() const { return m_draw_calls; }
    size_t MemoryBytes() const { return m_memory_bytes; }

    static const char *StageName(Stage s);

private:
    bool m_paused = false;
    std::chrono::steady_clock::time_point m_frame_start{};
    std::array<std::chrono::steady_clock::time_point, StageCount> m_stage_start{};
    std::array<float, StageCount> m_stage_accum{};
    std::array<Series, StageCount> m_stages{};
    Series m_total;
    Series m_entities_series;
    Series m_draw_calls_series;
    Series m_memory_series;
    int m_entities = 0;
    int m_draw_calls = 0;
    size_t m_memory_bytes = 0;
};

inline void Profiler::StartFrame()
{
    if (m_paused)
        return;
    m_frame_start = std::chrono::steady_clock::now();
    m_stage_accum.fill(0.0f);
}

inline void Profiler::EndFrame()
{
    if (m_paused)
        return;
    const auto now = std::chrono::steady_clock::now();
    const float total = std::chrono::duration<float>(now - m_frame_start).count();
    for (int s = 0; s < StageCount; ++s)
        m_stages[(size_t)s].Push(m_stage_accum[(size_t)s]);
    m_total.Push(total);
}

inline void Profiler::BeginStage(Stage s)
{
    if (m_paused)
        return;
    m_stage_start[(size_t)s] = std::chrono::steady_clock::now();
}

inline void Profiler::EndStage(Stage s)
{
    if (m_paused)
        return;
    m_stage_accum[(size_t)s] +=
        std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                     m_stage_start[(size_t)s]).count();
}

inline void Profiler::RecordResources(int entities, int draw_calls,
                                      size_t memory_bytes)
{
    m_entities = entities;
    m_draw_calls = draw_calls;
    m_memory_bytes = memory_bytes;
    if (m_paused)
        return;
    m_entities_series.Push((float)entities);
    m_draw_calls_series.Push((float)draw_calls);
    m_memory_series.Push((float)memory_bytes);
}

inline void Profiler::Clear()
{
    for (auto &s : m_stages)
        s.Clear();
    m_total.Clear();
    m_entities_series.Clear();
    m_draw_calls_series.Clear();
    m_memory_series.Clear();
    m_stage_accum.fill(0.0f);
    m_entities = 0;
    m_draw_calls = 0;
    m_memory_bytes = 0;
}

inline const char *Profiler::StageName(Stage s)
{
    switch (s)
    {
        case Update:   return "Update";
        case Render:   return "Render";
        case UI:       return "UI";
        case Physics:  return "Physics";
        default:       return "?";
    }
}
