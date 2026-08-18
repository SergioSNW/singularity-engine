#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct SDL_KeyboardEvent;
struct SDL_MouseButtonEvent;

// Phase 41 — Centralized Input Manager.
//
// Decouples physical hardware inputs from game logic using Action Mapping
// (digital on/off bindings like "Jump" -> Space) and Axis Mapping (analog
// +/- bindings like "MoveForward" -> W(+1) / S(-1)).
//
// Usage:
//   Input::Instance().RegisterAction("Jump", SDL_SCANCODE_SPACE);
//   Input::Instance().RegisterAxis("MoveForward", SDL_SCANCODE_W, SDL_SCANCODE_S);
//   if (Input::Instance().GetActionDown("Jump")) { ... }
//   float fwd = Input::Instance().GetAxis("MoveForward");

// SDL scancode type alias for readability.
using KeyCode = int;

class Input
{
public:
    static Input &Instance();

    // Frame lifecycle — called from Application::Run().
    void NewFrame();   // cache keyboard state before SDL_PollEvent
    void EndFrame();   // copy current -> previous after event polling

    // --- Action Mapping (digital) ---
    void RegisterAction(const std::string &name, KeyCode key);
    void ClearActions();

    bool GetAction(const std::string &name) const;
    bool GetActionDown(const std::string &name) const;
    bool GetActionUp(const std::string &name) const;

    // --- Axis Mapping (analog) ---
    void RegisterAxis(const std::string &name, KeyCode pos_key, KeyCode neg_key);
    void RegisterAxis(const std::string &name,
                      KeyCode pos_key, float pos_scale,
                      KeyCode neg_key, float neg_scale);
    void ClearAxes();

    float GetAxis(const std::string &name) const;

    // --- Raw key queries ---
    bool GetKey(KeyCode key) const;
    bool GetKeyDown(KeyCode key) const;
    bool GetKeyUp(KeyCode key) const;

    // --- Mouse ---
    int GetMouseX() const;
    int GetMouseY() const;
    void GetMousePosition(int &x, int &y) const;
    bool GetMouseButton(int button) const;
    bool GetMouseButtonDown(int button) const;
    bool GetMouseButtonUp(int button) const;

    // --- Event dispatch (called from SDL_PollEvent loop) ---
    void OnKeyDown(const SDL_KeyboardEvent &event);
    void OnKeyUp(const SDL_KeyboardEvent &event);
    void OnMouseButtonDown(const SDL_MouseButtonEvent &event);
    void OnMouseButtonUp(const SDL_MouseButtonEvent &event);
    void OnMouseMove(int x, int y);

private:
    Input();

    // Keyboard state: index = SDL_Scancode.
    static const int kMaxScancodes = 512;
    unsigned char m_current_keys[kMaxScancodes] = {};
    unsigned char m_previous_keys[kMaxScancodes] = {};

    // Frame transition buffer: keys pressed/released this frame (from events).
    bool m_key_down_events[kMaxScancodes] = {};
    bool m_key_up_events[kMaxScancodes] = {};

    // Mouse state.
    int m_mouse_x = 0;
    int m_mouse_y = 0;
    static const int kMaxMouseButtons = 8;
    bool m_mouse_current[kMaxMouseButtons] = {};
    bool m_mouse_previous[kMaxMouseButtons] = {};
    bool m_mouse_down_events[kMaxMouseButtons] = {};
    bool m_mouse_up_events[kMaxMouseButtons] = {};

    // Action maps: name -> scancode.
    std::unordered_map<std::string, KeyCode> m_actions;

    // Axis maps: name -> (pos_key, pos_scale, neg_key, neg_scale).
    struct AxisBinding
    {
        KeyCode pos_key;
        float pos_scale;
        KeyCode neg_key;
        float neg_scale;
    };
    std::unordered_map<std::string, AxisBinding> m_axes;
};
