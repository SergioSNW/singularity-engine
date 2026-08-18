#include "Input.h"

#include <SDL.h>
#include <algorithm>

Input &Input::Instance()
{
    static Input s_instance;
    return s_instance;
}

Input::Input() = default;

// --- Frame lifecycle ---

void Input::NewFrame()
{
    // Snapshot the live SDL keyboard state into our current buffer.
    int num_keys = 0;
    const unsigned char *live = SDL_GetKeyboardState(&num_keys);
    const int count = std::min(num_keys, kMaxScancodes);
    std::copy(live, live + count, m_current_keys);

    // Snapshot live mouse state.
    int mx = 0, my = 0;
    const Uint32 btn = SDL_GetMouseState(&mx, &my);
    m_mouse_x = mx;
    m_mouse_y = my;
    for (int i = 0; i < kMaxMouseButtons; ++i)
        m_mouse_current[i] = (btn & SDL_BUTTON(i)) != 0;
}

void Input::EndFrame()
{
    // Copy current -> previous so GetKeyDown/GetKeyUp can compare.
    std::copy(m_current_keys, m_current_keys + kMaxScancodes, m_previous_keys);
    std::copy(m_mouse_current, m_mouse_current + kMaxMouseButtons, m_mouse_previous);

    // Clear per-frame event flags.
    std::fill(m_key_down_events, m_key_down_events + kMaxScancodes, false);
    std::fill(m_key_up_events, m_key_up_events + kMaxScancodes, false);
    std::fill(m_mouse_down_events, m_mouse_down_events + kMaxMouseButtons, false);
    std::fill(m_mouse_up_events, m_mouse_up_events + kMaxMouseButtons, false);
}

// --- Action Mapping ---

void Input::RegisterAction(const std::string &name, KeyCode key)
{
    m_actions[name] = key;
}

void Input::ClearActions()
{
    m_actions.clear();
}

bool Input::GetAction(const std::string &name) const
{
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    const int sc = it->second;
    if (sc < 0 || sc >= kMaxScancodes)
        return false;
    return m_current_keys[sc] != 0;
}

bool Input::GetActionDown(const std::string &name) const
{
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    const int sc = it->second;
    if (sc < 0 || sc >= kMaxScancodes)
        return false;
    return m_current_keys[sc] != 0 && m_previous_keys[sc] == 0;
}

bool Input::GetActionUp(const std::string &name) const
{
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    const int sc = it->second;
    if (sc < 0 || sc >= kMaxScancodes)
        return false;
    return m_current_keys[sc] == 0 && m_previous_keys[sc] != 0;
}

// --- Axis Mapping ---

void Input::RegisterAxis(const std::string &name, KeyCode pos_key, KeyCode neg_key)
{
    m_axes[name] = { pos_key, 1.0f, neg_key, 1.0f };
}

void Input::RegisterAxis(const std::string &name,
                          KeyCode pos_key, float pos_scale,
                          KeyCode neg_key, float neg_scale)
{
    m_axes[name] = { pos_key, pos_scale, neg_key, neg_scale };
}

void Input::ClearAxes()
{
    m_axes.clear();
}

float Input::GetAxis(const std::string &name) const
{
    auto it = m_axes.find(name);
    if (it == m_axes.end())
        return 0.0f;

    const AxisBinding &b = it->second;
    float value = 0.0f;

    if (b.pos_key >= 0 && b.pos_key < kMaxScancodes && m_current_keys[b.pos_key])
        value += b.pos_scale;
    if (b.neg_key >= 0 && b.neg_key < kMaxScancodes && m_current_keys[b.neg_key])
        value -= b.neg_scale;

    // Clamp to [-1, 1] when both scales are 1.0 (standard axis).
    if (b.pos_scale == 1.0f && b.neg_scale == 1.0f)
        value = std::clamp(value, -1.0f, 1.0f);

    return value;
}

// --- Raw key queries ---

bool Input::GetKey(KeyCode key) const
{
    if (key < 0 || key >= kMaxScancodes)
        return false;
    return m_current_keys[key] != 0;
}

bool Input::GetKeyDown(KeyCode key) const
{
    if (key < 0 || key >= kMaxScancodes)
        return false;
    return m_current_keys[key] != 0 && m_previous_keys[key] == 0;
}

bool Input::GetKeyUp(KeyCode key) const
{
    if (key < 0 || key >= kMaxScancodes)
        return false;
    return m_current_keys[key] == 0 && m_previous_keys[key] != 0;
}

// --- Mouse ---

int Input::GetMouseX() const { return m_mouse_x; }
int Input::GetMouseY() const { return m_mouse_y; }

void Input::GetMousePosition(int &x, int &y) const
{
    x = m_mouse_x;
    y = m_mouse_y;
}

bool Input::GetMouseButton(int button) const
{
    if (button < 0 || button >= kMaxMouseButtons)
        return false;
    return m_mouse_current[button];
}

bool Input::GetMouseButtonDown(int button) const
{
    if (button < 0 || button >= kMaxMouseButtons)
        return false;
    return m_mouse_current[button] && !m_mouse_previous[button];
}

bool Input::GetMouseButtonUp(int button) const
{
    if (button < 0 || button >= kMaxMouseButtons)
        return false;
    return !m_mouse_current[button] && m_mouse_previous[button];
}

// --- Event dispatch ---

void Input::OnKeyDown(const SDL_KeyboardEvent &event)
{
    const int sc = event.keysym.scancode;
    if (sc >= 0 && sc < kMaxScancodes)
        m_key_down_events[sc] = true;
}

void Input::OnKeyUp(const SDL_KeyboardEvent &event)
{
    const int sc = event.keysym.scancode;
    if (sc >= 0 && sc < kMaxScancodes)
        m_key_up_events[sc] = true;
}

void Input::OnMouseButtonDown(const SDL_MouseButtonEvent &event)
{
    const int btn = event.button - 1; // SDL_BUTTON_LEFT = 1
    if (btn >= 0 && btn < kMaxMouseButtons)
        m_mouse_down_events[btn] = true;
}

void Input::OnMouseButtonUp(const SDL_MouseButtonEvent &event)
{
    const int btn = event.button - 1;
    if (btn >= 0 && btn < kMaxMouseButtons)
        m_mouse_up_events[btn] = true;
}

void Input::OnMouseMove(int x, int y)
{
    m_mouse_x = x;
    m_mouse_y = y;
}
