#pragma once

#include <string>
#include <vector>

// Severity of a console entry. Direct API writers (Lua print, ScriptEngine,
// Application) choose a level; bytes drained from the redirected stdout/stderr
// pipes have their severity approximated by stream (stdout -> Info,
// stderr -> Error).
enum class LogLevel
{
    Info,
    Warning,
    Error,
};

struct LogEntry
{
    LogLevel level;
    std::string text;
};

// Shared engine log sink.
//
// Every engine subsystem writes here instead of a terminal: Lua print() is
// overridden to funnel through Console::Write, ScriptEngine routes bind errors
// and runtime exceptions here, and any C-level stdout/stderr output (printf,
// std::cout, stray fprintf) is captured through two OS pipes that are drained
// without blocking each frame. All pipe access happens on the UI thread (the
// same thread that runs Lua and physics), so no locking is needed.
class Console
{
public:
    static Console &Instance();

    // Append one entry with an explicit severity. Pending piped bytes are
    // drained first so interleaved output stays ordered.
    void Write(LogLevel level, const std::string &text);

    // Redirect C stdout/stderr into the console via two OS pipes. Output that
    // arrives through the pipes has its severity approximated by stream
    // (stdout -> Info, stderr -> Error). Idempotent; returns false if the OS
    // refused the redirect (the engine then relies on direct Write() calls).
    bool StartRedirect();
    void StopRedirect();

    // Pull any pending piped bytes into entries. Call once per frame from the
    // UI (the ConsolePanel does this).
    void DrainPipes();

    // Discard every entry. Pending piped bytes are drained first so nothing
    // resurfaces after the clear.
    void Clear();

    size_t Size() const { return m_entries.size(); }
    const LogEntry &Entry(size_t i) const { return m_entries[i]; }

private:
    Console();
    ~Console();
    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;

    void DrainOne(void *read_handle, LogLevel level, std::string &line_buffer);

    std::vector<LogEntry> m_entries;
    void *m_stdout_pipe = nullptr;   // read handle for redirected stdout (HANDLE)
    void *m_stderr_pipe = nullptr;   // read handle for redirected stderr (HANDLE)
    std::string m_stdout_line;       // partial stdout line awaiting a newline
    std::string m_stderr_line;       // partial stderr line awaiting a newline
};

// Convenience free functions used across core/ and script/.
void ConsoleInfo(const std::string &text);
void ConsoleWarning(const std::string &text);
void ConsoleError(const std::string &text);
