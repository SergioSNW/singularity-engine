#pragma once

#include <string>

// Stage 3: minimal runtime-only gameplay state. A Lua script drives it
// through the Game.* API (ScriptEngine.cpp: Game.SetHealth/AddScore/
// ShowPrompt/Win/Lose/...), and the gameplay HUD overlay (Application.cpp)
// reads it every frame to render health/score/prompt and the win/lose
// screen. Deliberately not part of any component and never serialized --
// this is Play-session state, reset to defaults every time EnterPlayMode()
// runs, not scene data that should survive a save/load.
//
// health/score are intentionally just numbers with no built-in meaning (no
// "health <= 0 auto-loses", no fixed win condition): a level's own script
// decides what they mean and when Win()/Lose() actually fires, the same way
// the engine doesn't hardcode what a Wall or a Trigger Zone does. This
// keeps the HUD reusable across genres, matching the whole engine's
// "the tool doesn't assume what game you're making" stance.
struct GameplayState
{
    enum class Status { Playing, Won, Lost };

    int health = 100;
    int score = 0;
    std::string prompt;   // empty = no prompt banner shown
    Status status = Status::Playing;

    // Stage 6: set by Game.LoadScene(path) (ScriptEngine.cpp), consumed by
    // Application once per frame at the one safe point after every
    // per-frame system has finished walking the entity list -- see
    // SceneTransition.h. Empty = no scene load requested this frame.
    std::string pending_scene;
};
