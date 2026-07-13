#include "pch.h"

#include "Tool/ProcessSession.h"

#include <array>

namespace VisualForge::Tool
{
    namespace
    {
        HANDLE AsHandle(void* value) noexcept
        {
            return static_cast<HANDLE>(value);
        }

        void CloseIfValid(void*& value) noexcept
        {
            auto handle = AsHandle(value);
            if (handle && handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
            }

            value = nullptr;
        }
    }

    ProcessSession::~ProcessSession()
    {
        Stop();
        JoinReaders();
        CloseHandles();
    }

    void ProcessSession::JoinReaders() noexcept
    {
        if (m_stdoutReader.joinable())
        {
            m_stdoutReader.join();
        }

        if (m_stderrReader.joinable())
        {
            m_stderrReader.join();
        }
    }

    bool ProcessSession::Start(ToolCommand const& command)
    {
        if (IsRunning())
        {
            return false;
        }

        // A session can be reused after a previous process exits. Join the old
        // readers before assigning new reader threads.
        JoinReaders();
        CloseHandles();
        {
            std::scoped_lock lock{ m_outputLock };
            m_stdout.clear();
            m_stderr.clear();
        }
        m_exitCode = 0;
        m_state = ProcessSessionState::NotStarted;

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE stdoutRead{};
        HANDLE stdoutWrite{};
        HANDLE stderrRead{};
        HANDLE stderrWrite{};
        HANDLE stdinRead{};
        HANDLE stdinWrite{};

        if (!CreatePipe(&stdoutRead, &stdoutWrite, &securityAttributes, 0)
            || !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)
            || !CreatePipe(&stderrRead, &stderrWrite, &securityAttributes, 0)
            || !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)
            || !CreatePipe(&stdinRead, &stdinWrite, &securityAttributes, 0)
            || !SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0))
        {
            if (stdoutRead) CloseHandle(stdoutRead);
            if (stdoutWrite) CloseHandle(stdoutWrite);
            if (stderrRead) CloseHandle(stderrRead);
            if (stderrWrite) CloseHandle(stderrWrite);
            if (stdinRead) CloseHandle(stdinRead);
            if (stdinWrite) CloseHandle(stdinWrite);
            m_state = ProcessSessionState::Failed;
            return false;
        }

        auto commandLine = command.ToCommandLine();
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = stdinRead;
        startupInfo.hStdOutput = stdoutWrite;
        startupInfo.hStdError = stderrWrite;

        PROCESS_INFORMATION processInfo{};
        auto workingDirectory = command.WorkingDirectory.wstring();
        auto const created = CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            &startupInfo,
            &processInfo);

        CloseHandle(stdoutWrite);
        CloseHandle(stderrWrite);
        CloseHandle(stdinRead);

        if (!created)
        {
            CloseHandle(stdoutRead);
            CloseHandle(stderrRead);
            CloseHandle(stdinWrite);
            m_state = ProcessSessionState::Failed;
            return false;
        }

        m_process = processInfo.hProcess;
        m_thread = processInfo.hThread;
        m_stdinWrite = stdinWrite;
        m_exitCode = 0;
        m_state = ProcessSessionState::Running;
        StartReaders(stdoutRead, stderrRead);
        return true;
    }

    bool ProcessSession::Write(std::string_view bytes)
    {
        if (!IsRunning() || !m_stdinWrite)
        {
            return false;
        }

        DWORD written{};
        return WriteFile(AsHandle(m_stdinWrite), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
            && static_cast<std::size_t>(written) == bytes.size();
    }

    bool ProcessSession::WaitForExit(unsigned long timeoutMs) noexcept
    {
        if (!m_process)
        {
            return false;
        }

        auto const result = WaitForSingleObject(AsHandle(m_process), timeoutMs);
        if (result == WAIT_OBJECT_0)
        {
            DWORD exitCode{};
            if (GetExitCodeProcess(AsHandle(m_process), &exitCode))
            {
                m_exitCode = exitCode;
            }

            m_state = ProcessSessionState::Exited;
            CloseIfValid(m_stdinWrite);
            return true;
        }

        return false;
    }

    void ProcessSession::Stop(unsigned int exitCode) noexcept
    {
        if (IsRunning() && m_process)
        {
            TerminateProcess(AsHandle(m_process), exitCode);
            m_exitCode = exitCode;
            m_state = ProcessSessionState::Exited;
        }

        CloseIfValid(m_stdinWrite);
    }

    ProcessOutputSnapshot ProcessSession::Snapshot() const
    {
        if (m_process && m_state == ProcessSessionState::Running)
        {
            DWORD exitCode{};
            if (GetExitCodeProcess(AsHandle(m_process), &exitCode) && exitCode != STILL_ACTIVE)
            {
                m_exitCode = exitCode;
                m_state = ProcessSessionState::Exited;
            }
        }

        std::scoped_lock lock{ m_outputLock };
        return { m_stdout, m_stderr, m_exitCode.load(), m_state.load() };
    }

    ProcessSessionState ProcessSession::State() const noexcept
    {
        return m_state.load();
    }

    bool ProcessSession::IsRunning() const noexcept
    {
        return m_state.load() == ProcessSessionState::Running;
    }

    void ProcessSession::CloseHandles() noexcept
    {
        CloseIfValid(m_stdinWrite);
        CloseIfValid(m_thread);
        CloseIfValid(m_process);
    }

    void ProcessSession::StartReaders(void* stdoutRead, void* stderrRead)
    {
        m_stdoutReader = std::thread{ [this, stdoutRead]
        {
            ReadLoop(stdoutRead, false);
        } };

        m_stderrReader = std::thread{ [this, stderrRead]
        {
            ReadLoop(stderrRead, true);
        } };
    }

    void ProcessSession::ReadLoop(void* handle, bool stderrStream)
    {
        std::array<char, 4096> buffer{};
        DWORD bytesRead{};

        while (ReadFile(AsHandle(handle), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0)
        {
            std::scoped_lock lock{ m_outputLock };
            auto& output = stderrStream ? m_stderr : m_stdout;
            output.append(buffer.data(), bytesRead);
        }

        CloseHandle(AsHandle(handle));
    }
}
