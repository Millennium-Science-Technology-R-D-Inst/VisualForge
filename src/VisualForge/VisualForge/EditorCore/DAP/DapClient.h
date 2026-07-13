#pragma once

#include "EditorCore/LSP/JsonRpc.h"
#include "EditorCore/DAP/DapProtocol.h"
#include "Tool/ProcessSession.h"

#include <memory>
#include <string>
#include <vector>

namespace VisualForge::EditorCore::DAP
{
    enum class DapClientState
    {
        Stopped,
        Running,
        Faulted
    };

    class DapClient final
    {
    public:
        [[nodiscard]] bool Start(std::wstring lldbDapPath, std::filesystem::path workingDirectory = {});
        void Stop();

        void Initialize();
        void Launch(std::wstring const& program, std::wstring const& arguments, std::wstring const& cwd, bool stopAtEntry);
        void SetBreakpoints(std::wstring const& sourcePath, std::vector<int> const& lines);
        void Continue(int threadId);
        void Next(int threadId);
        void StepIn(int threadId);
        void StepOut(int threadId);
        void RequestStackTrace(int threadId);
        void RequestScopes(int frameId);
        void RequestVariables(int variablesReference);
        void ConfigurationDone();
        void Disconnect();

        [[nodiscard]] std::vector<LSP::JsonRpcMessage> DrainReceivedMessages();
        [[nodiscard]] std::vector<DapProtocolMessage> DrainProtocolMessages();
        [[nodiscard]] DapClientState State() const noexcept;
        [[nodiscard]] std::vector<std::string> const& SentMessages() const noexcept;
        [[nodiscard]] Tool::ProcessOutputSnapshot ProcessSnapshot() const;

    private:
        void Send(std::string payload);
        [[nodiscard]] int NextSequence() noexcept;
        [[nodiscard]] static std::string Narrow(std::wstring const& value);

        DapClientState m_state{ DapClientState::Stopped };
        int m_nextSequence{ 1 };
        std::vector<std::string> m_sentMessages;
        std::unique_ptr<Tool::ProcessSession> m_process;
        LSP::JsonRpcStreamParser m_parser;
        std::size_t m_stdoutBytesConsumed{ 0 };
    };
}
