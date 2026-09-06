#pragma once

#include <functional>
#include <string>

// Stage 7: the bridge behind Lua's UI.* bindings. ScriptEngine.cpp stays free
// of any ImGui dependency (the same discipline AudioManager/GameplayState's
// own bridges already follow) by talking only to this plain-data/callback
// struct; Application wires real ImGui-backed callbacks into it right before
// calling ScriptEngine::RenderGUI() each frame Play is active, and clears
// them again right after -- same observer-pointer lifetime as every other
// bridge here.
//
// All coordinates are pixels relative to the top-left corner of the play
// viewport (0,0 = top-left), the same space Application::RenderGameplayHUD's
// own health/score/prompt boxes already draw in -- so a script's UI sits in
// the same coordinate system as the built-in HUD elements around it, and
// UI.Width()/UI.Height() give a script everything it needs to lay out
// relative to the actual play area instead of a fixed resolution.
struct UIContext
{
    float width = 0.0f;
    float height = 0.0f;

    std::function<void(float x, float y, const std::string &text,
                       float r, float g, float b, float a)> draw_text;
    std::function<void(float x, float y, float w, float h,
                       float r, float g, float b, float a)> draw_rect;
    // Returns true on the exact frame the button is clicked.
    std::function<bool(float x, float y, float w, float h,
                       const std::string &text)> draw_button;
};
