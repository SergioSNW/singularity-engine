#include "core/ToastManager.h"

#include <algorithm>

void ToastManager::Push(const std::string &text, uint64_t now_ms, uint64_t lifetime_ms)
{
    if (text.empty())
        return;

    // A repeated message refreshes instead of stacking a duplicate line.
    if (!m_toasts.empty() && m_toasts.back().text == text)
    {
        m_toasts.back().born_ms = now_ms;
        m_toasts.back().expire_ms = now_ms + lifetime_ms;
        return;
    }

    Toast t;
    t.text = text;
    t.born_ms = now_ms;
    t.expire_ms = now_ms + lifetime_ms;
    m_toasts.push_back(t);

    if (m_toasts.size() > MaxToasts)
        m_toasts.erase(m_toasts.begin());
}

void ToastManager::Update(uint64_t now_ms)
{
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
                                  [now_ms](const Toast &t)
                                  { return now_ms >= t.expire_ms; }),
                   m_toasts.end());
}

float ToastManager::NewestFade(uint64_t now_ms) const
{
    if (m_toasts.empty())
        return 0.0f;
    const Toast &t = m_toasts.back();
    const uint64_t elapsed = now_ms - t.born_ms;
    const uint64_t lifetime = t.expire_ms - t.born_ms;
    if (lifetime == 0)
        return 0.0f;
    const uint64_t remaining = lifetime > elapsed ? lifetime - elapsed : 0;
    if (remaining >= FadeOutMs)
        return 1.0f;
    return (float)remaining / (float)FadeOutMs;
}
