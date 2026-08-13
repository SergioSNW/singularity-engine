#include "AudioManager.h"

#include "core/Console.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <map>
#include <string>
#include <vector>

namespace {

// Mixer channel pool size: enough distinct one-shots/loops for the small
// scenes this engine targets without reserving the whole 256 SDL_mixer
// channels (each channel has a volume + converter, so keeping it small is a
// thermal win on constrained hardware).
constexpr int kMixerChannels = 16;

AudioManager *g_audio = nullptr;

float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

struct AudioManager::Impl
{
    bool ready = false;
    float master = 1.0f;

    // Decoded samples, cached for the manager's lifetime. Loading is lazy so
    // the first Play pays the decode cost and every later one is a lookup.
    std::map<std::string, Mix_Chunk *> chunks;

    // Paths logged as unloadable (dedupe so a missing asset is reported once,
    // not every frame). The file is still retried on the next Play.
    std::vector<std::string> failed;

    // channel id -> source path for every channel this manager started, so
    // Stop(path) can halt exactly the right channels. The Mix_ChannelFinished
    // callback erases entries as channels end naturally.
    std::map<int, std::string> channel_path;
};

AudioManager::AudioManager()
    : m_impl(new Impl())
{
}

AudioManager::~AudioManager()
{
    Shutdown();
    delete m_impl;
    m_impl = nullptr;
}

// SDL_mixer invokes this from its audio thread when a channel finishes; it is
// the only place a channel can end without an explicit Stop call, so it keeps
// channel_path accurate for the next Stop(path).
void AudioManager::OnChannelFinished(int channel)
{
    if (g_audio)
        g_audio->m_impl->channel_path.erase(channel);
}

bool AudioManager::Init()
{
    if (!m_impl)
        return false;
    if (m_impl->ready)
        return true;

    // The mixer needs the SDL audio subsystem; make sure it exists even if a
    // caller never ran the full SDL_Init with audio flags.
    SDL_InitSubSystem(SDL_INIT_AUDIO);

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
    {
        ConsoleError(std::string("[AudioManager] Mix_OpenAudio failed: ") +
                     Mix_GetError());
        return false;
    }

    Mix_AllocateChannels(kMixerChannels);
    g_audio = this;
    Mix_ChannelFinished(&AudioManager::OnChannelFinished);
    m_impl->ready = true;
    return true;
}

void AudioManager::Shutdown()
{
    if (!m_impl)
        return;
    if (m_impl->ready)
    {
        Mix_HaltChannel(-1);
        g_audio = nullptr;
        m_impl->channel_path.clear();
        for (auto &entry : m_impl->chunks)
            Mix_FreeChunk(entry.second);
        m_impl->chunks.clear();
        Mix_CloseAudio();
        m_impl->ready = false;
    }
}

bool AudioManager::IsReady() const
{
    return m_impl && m_impl->ready;
}

Mix_Chunk *AudioManager::LoadChunk(const std::string &path)
{
    auto it = m_impl->chunks.find(path);
    if (it != m_impl->chunks.end())
        return it->second;

    Mix_Chunk *chunk = Mix_LoadWAV(path.c_str());
    if (!chunk)
    {
        bool known = false;
        for (const std::string &p : m_impl->failed)
            if (p == path)
                known = true;
        if (!known)
        {
            m_impl->failed.push_back(path);
            ConsoleError(std::string("[AudioManager] cannot load '") + path +
                         "': " + Mix_GetError());
        }
        return nullptr;
    }

    m_impl->chunks[path] = chunk;
    return chunk;
}

int AudioManager::Play(const std::string &path, float volume, bool loop)
{
    if (!m_impl || !m_impl->ready || path.empty())
        return -1;

    Mix_Chunk *chunk = LoadChunk(path);
    if (!chunk)
        return -1;

    // loops < 0 makes SDL_mixer repeat forever; 0 plays once.
    const int channel = Mix_PlayChannel(-1, chunk, loop ? -1 : 0);
    if (channel < 0)
        return -1;

    Mix_Volume(channel,
               (int)(Clamp01(volume) * Clamp01(m_impl->master) *
                     (float)MIX_MAX_VOLUME));
    m_impl->channel_path[channel] = path;
    return channel;
}

void AudioManager::Stop(const std::string &path)
{
    if (!m_impl || !m_impl->ready || path.empty())
        return;

    // Collect first, then halt: the ChannelFinished callback mutates
    // channel_path while we iterate, so we never touch the map mid-loop.
    std::vector<int> matches;
    for (auto &entry : m_impl->channel_path)
        if (entry.second == path)
            matches.push_back(entry.first);
    for (int channel : matches)
        Mix_HaltChannel(channel);
}

void AudioManager::StopAll()
{
    if (!m_impl || !m_impl->ready)
        return;
    Mix_HaltChannel(-1);
    m_impl->channel_path.clear();
}

int AudioManager::ActiveChannelCount() const
{
    if (!m_impl || !m_impl->ready)
        return 0;
    return (int)m_impl->channel_path.size();
}

void AudioManager::SetMasterVolume(float volume)
{
    if (!m_impl)
        return;
    m_impl->master = Clamp01(volume);
    if (m_impl->ready)
        Mix_Volume(-1, (int)(m_impl->master * (float)MIX_MAX_VOLUME));
}

float AudioManager::MasterVolume() const
{
    return m_impl ? m_impl->master : 1.0f;
}
