#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Timed, stackable notification list (Phase 28). The Application feeds wall
// clock milliseconds (SDL_GetTicks) in each frame; the manager prunes expired
// toasts and hands back the survivors so the UI can render them as a
// top-right overlay. Kept free of any UI/engine dependency so it runs in the
// headless harness.
class ToastManager
{
public:
    struct Toast
    {
        std::string text;
        uint64_t born_ms;
        uint64_t expire_ms;
    };

    // Lifetime a toast gets when pushed. The visible list is capped at
    // MaxToasts(); older entries drop off the bottom.
    static constexpr uint64_t DefaultLifetimeMs = 3500;
    static constexpr uint64_t FadeOutMs = 400;
    static constexpr size_t MaxToasts = 5;

    // Push a new toast. Oldest entries are evicted once the list is full.
    void Push(const std::string &text, uint64_t now_ms, uint64_t lifetime_ms = DefaultLifetimeMs);

    // Remove toasts whose expire_ms <= now_ms. Call once per frame.
    void Update(uint64_t now_ms);

    void Clear() { m_toasts.clear(); }

    bool empty() const { return m_toasts.empty(); }
    size_t Count() const { return m_toasts.size(); }
    const Toast *Get(size_t i) const
    {
        return i < m_toasts.size() ? &m_toasts[i] : nullptr;
    }

    // 1.0 for a fresh toast, linearly dropping to 0 across the last
    // FadeOutMs of the *newest* toast (or 0 when nothing is visible).
    float NewestFade(uint64_t now_ms) const;

private:
    std::vector<Toast> m_toasts;
};
