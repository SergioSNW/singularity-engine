#pragma once

#include <string>

struct Mix_Chunk;  // SDL_mixer sample type; defined only in AudioManager.cpp

// Audio playback manager for the Phase 26 sound subsystem. A thin wrapper over
// SDL_mixer that loads sound-effect samples (WAV / OGG) on demand, caches them
// for the lifetime of the manager, and plays them on a small pool of mixer
// channels. SDL_mixer types stay entirely inside AudioManager.cpp, so this
// header is dependency-free and other modules can hold an AudioManager*
// without pulling the mixer headers in.
//
// The manager is designed to be optional at runtime: if the audio device
// cannot be opened (headless machines, no sound hardware), Init() reports
// false and every later call degrades to a silent no-op instead of crashing.
class AudioManager
{
public:
    AudioManager();
    ~AudioManager();

    // Open the SDL_mixer audio device (44.1 kHz stereo, a 16-channel mixer
    // pool). Idempotent: repeated calls while already running are no-ops.
    // Returns false when the device could not be opened.
    bool Init();

    // Close the device and free every cached sample. Idempotent.
    void Shutdown();

    bool IsReady() const;

    // Load `path` on demand and play it on a free channel at `volume`
    // (clamped to 0..1); `loop` repeats until stopped. Returns the channel id
    // the sample started on, or -1 when the file could not be loaded or no
    // channel was free. Missing files are reported once and retried on the
    // next Play (so an asset dropped in later starts working).
    int Play(const std::string &path, float volume, bool loop);

    // Stop every channel currently playing `path`.
    void Stop(const std::string &path);

    // Halt every playing channel (used when leaving play mode / previews).
    void StopAll();

    // Number of channels currently playing a sample (the status bar's "active
    // audio channels" metric). 0 when the mixer is not ready.
    int ActiveChannelCount() const;

    // Master gain (0..1) applied to every channel. New Play calls scale the
    // sample by it; already-running channels are re-gained immediately.
    void SetMasterVolume(float volume);
    float MasterVolume() const;

private:
    // SDL_mixer channel-finished hook (fired from the audio thread); erases
    // the finished channel's bookkeeping so Stop(path) stays accurate.
    static void OnChannelFinished(int channel);

    Mix_Chunk *LoadChunk(const std::string &path);

    struct Impl;
    Impl *m_impl;
};
