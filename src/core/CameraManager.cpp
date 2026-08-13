#include "CameraManager.h"

#include <algorithm>
#include <cmath>

CameraManager::CameraManager() = default;

int CameraManager::Add(const CameraEntry &entry)
{
    m_entries.push_back(entry);
    return (int)m_entries.size() - 1;
}

bool CameraManager::Remove(size_t index)
{
    if (index >= m_entries.size())
        return false;
    m_entries.erase(m_entries.begin() + index);
    // Keep the primary invariant: exactly one primary entry. Removing the
    // primary promotes the topmost enabled entry so the editor always has a
    // target for input/picking.
    int primary = -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].primary)
        {
            primary = (int)i;
            break;
        }
    if (primary < 0)
    {
        int fallback = -1;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (!m_entries[i].enabled)
                continue;
            if (fallback < 0 ||
                m_entries[i].z > m_entries[(size_t)fallback].z ||
                (m_entries[i].z == m_entries[(size_t)fallback].z && i > (size_t)fallback))
                fallback = (int)i;
        }
        if (fallback >= 0)
            m_entries[(size_t)fallback].primary = true;
    }
    return true;
}

void CameraManager::Clear()
{
    m_entries.clear();
}

void CameraManager::SetPrimary(size_t index)
{
    if (index >= m_entries.size())
        return;
    for (CameraEntry &e : m_entries)
        e.primary = false;
    m_entries[index].primary = true;
}

std::vector<size_t> CameraManager::DrawOrder() const
{
    std::vector<size_t> order;
    order.reserve(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); ++i)
        order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
        return m_entries[a].z < m_entries[b].z;
    });
    return order;
}

int CameraManager::PrimaryIndex() const
{
    if (m_entries.empty())
        return -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].primary && m_entries[i].enabled)
            return (int)i;
    // Fallback: topmost enabled entry (largest z; on ties the later added).
    // A disabled primary does not own editor input, so it falls through here.
    int best = -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (!m_entries[i].enabled)
            continue;
        if (best < 0 ||
            m_entries[i].z > m_entries[(size_t)best].z ||
            (m_entries[i].z == m_entries[(size_t)best].z && i > (size_t)best))
            best = (int)i;
    }
    return best;
}

bool CameraManager::RectToPixels(const CameraEntry &e, int target_w, int target_h,
                                 int &px, int &py, int &pw, int &ph)
{
    if (target_w <= 0 || target_h <= 0 || e.w <= 0.0f || e.h <= 0.0f)
        return false;

    int x0 = (int)std::lround(e.x * target_w);
    int y0 = (int)std::lround(e.y * target_h);
    int x1 = (int)std::lround((e.x + e.w) * target_w);
    int y1 = (int)std::lround((e.y + e.h) * target_h);
    if (x1 <= x0 || y1 <= y0)
        return false;

    px = x0;
    py = y0;
    pw = x1 - x0;
    ph = y1 - y0;

    if (px < 0) { pw += px; px = 0; }
    if (py < 0) { ph += py; py = 0; }
    if (px + pw > target_w) pw = target_w - px;
    if (py + ph > target_h) ph = target_h - py;
    return pw > 0 && ph > 0;
}

void CameraManager::ResetToSingleViewport()
{
    m_entries.clear();
    CameraEntry entry;
    entry.label = "Main Viewport";
    entry.primary = true;
    m_entries.push_back(entry);
}
