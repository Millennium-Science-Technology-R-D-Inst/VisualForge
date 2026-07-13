#pragma once

#include "Tool/ToolCommand.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace VisualForge::Tool
{
    enum class ProcessSessionState
    {
        NotStarted,
        Running,
        Exited,
        Failed
    };

    struct ProcessOutputSnapshot
    {
        std::string StdOut;
        std::string StdErr;
        unsigned long ExitCode{ 0 };
        ProcessSessionState State{ ProcessSessionState::NotStarted };
    };

    class ProcessSession final
    {
    public:
        ProcessSession() = default;
        ~ProcessSession();

        ProcessSession(ProcessSession const&) = delete;
        ProcessSession& operator=(ProcessSession const&) = delete;

        [[nodiscard]] bool Start(ToolCommand const& command);
        [[nodiscard]] bool Write(std::string_view bytes);
        [[nodiscard]] bool WaitForExit(unsigned long timeoutMs = 0xFFFFFFFF) noexcept;
        void Stop(unsigned int exitCode = 1) noexcept;
        [[nodiscard]] ProcessOutputSnapshot Snapshot() const;
        [[nodiscard]] ProcessSessionState State() const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        void CloseHandles() noexcept;
        void JoinReaders() noexcept;
        void StartReaders(void* stdoutRead, void* stderrRead);
        void ReadLoop(void* handle, bool stderrStream);

        void* m_process{ nullptr };
        void* m_thread{ nullptr };
        void* m_stdinWrite{ nullptr };
        mutable std::mutex m_outputLock;
        std::string m_stdout;
        std::string m_stderr;
        std::thread m_stdoutReader;
        std::thread m_stderrReader;
        mutable std::atomic<ProcessSessionState> m_state{ ProcessSessionState::NotStarted };
        mutable std::atomic<unsigned long> m_exitCode{ 0 };
    };
}
