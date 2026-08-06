#include "core/Console.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#endif

namespace {

const size_t kMaxEntries = 4096;

// Create an OS pipe, make `write_handle` the CRT/Win32 standard stream `target`
// (1 = stdout, 2 = stderr), and hand the read end back in `read_handle_out`.
// The C runtime keeps the write end open as its fd, so printf/fwrite/std::cout
// all land in the pipe; setvbuf disables buffering so output is visible the
// instant it is written.
#ifdef _WIN32
bool CreateRedirectedPipe(void **read_handle_out, DWORD target)
{
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_h = nullptr, write_h = nullptr;
    if (!CreatePipe(&read_h, &write_h, &sa, 0))
        return false;
    // The write end is inheritable (so _dup2 keeps working), the read end is
    // not (so children cannot steal console output).
    SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0);

    if (!SetStdHandle(target, write_h))
    {
        CloseHandle(read_h);
        CloseHandle(write_h);
        return false;
    }

    int fd = _open_osfhandle((intptr_t)write_h, _O_WRONLY | _O_BINARY);
    if (fd < 0)
    {
        CloseHandle(read_h);
        CloseHandle(write_h);
        return false;
    }

    const int crt_fd = (target == STD_OUTPUT_HANDLE) ? 1 : 2;
    if (_dup2(fd, crt_fd) < 0)
    {
        CloseHandle(read_h);
        CloseHandle(write_h);
        return false;
    }

    setvbuf((crt_fd == 1) ? stdout : stderr, nullptr, _IONBF, 0);

    *read_handle_out = read_h;
    return true;
}
#endif

} // namespace

Console &Console::Instance()
{
    static Console instance;
    return instance;
}

Console::Console() {}

Console::~Console()
{
    StopRedirect();
}

void Console::Write(LogLevel level, const std::string &text)
{
    if (text.empty())
        return;
    DrainPipes();
    if (m_entries.size() >= kMaxEntries)
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + (long)(m_entries.size() - kMaxEntries + 1));
    m_entries.push_back({ level, text });
}

bool Console::StartRedirect()
{
    if (m_stdout_pipe || m_stderr_pipe)
        return true;                    // already redirected
#ifdef _WIN32
    void *handle = nullptr;
    if (CreateRedirectedPipe(&handle, STD_OUTPUT_HANDLE))
        m_stdout_pipe = handle;
    if (CreateRedirectedPipe(&handle, STD_ERROR_HANDLE))
        m_stderr_pipe = handle;
#endif
    return m_stdout_pipe || m_stderr_pipe;
}

void Console::StopRedirect()
{
    DrainPipes();
#ifdef _WIN32
    if (m_stdout_pipe)
    {
        CloseHandle((HANDLE)m_stdout_pipe);
        m_stdout_pipe = nullptr;
    }
    if (m_stderr_pipe)
    {
        CloseHandle((HANDLE)m_stderr_pipe);
        m_stderr_pipe = nullptr;
    }
#endif
}

void Console::DrainOne(void *read_handle, LogLevel level, std::string &line_buffer)
{
#ifdef _WIN32
    HANDLE h = (HANDLE)read_handle;
    char buf[1024];
    for (;;)
    {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
            break;
        const DWORD want = (avail < (DWORD)sizeof(buf)) ? avail : (DWORD)sizeof(buf);
        DWORD got = 0;
        if (!ReadFile(h, buf, want, &got, nullptr) || got == 0)
            break;
        line_buffer.append(buf, got);

        // Split complete lines out; a trailing partial line stays buffered
        // until its newline arrives. Windows CRLF is stripped per line.
        size_t pos;
        while ((pos = line_buffer.find('\n')) != std::string::npos)
        {
            std::string line = line_buffer.substr(0, pos);
            line_buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
            {
                if (m_entries.size() >= kMaxEntries)
                    m_entries.erase(m_entries.begin());
                m_entries.push_back({ level, line });
            }
        }
    }
#else
    (void)read_handle;
    (void)level;
    (void)line_buffer;
#endif
}

void Console::DrainPipes()
{
    if (m_stdout_pipe)
        DrainOne(m_stdout_pipe, LogLevel::Info, m_stdout_line);
    if (m_stderr_pipe)
        DrainOne(m_stderr_pipe, LogLevel::Error, m_stderr_line);
}

void Console::Clear()
{
    DrainPipes();
    m_entries.clear();
}

void ConsoleInfo(const std::string &text)
{
    Console::Instance().Write(LogLevel::Info, text);
}

void ConsoleWarning(const std::string &text)
{
    Console::Instance().Write(LogLevel::Warning, text);
}

void ConsoleError(const std::string &text)
{
    Console::Instance().Write(LogLevel::Error, text);
}
