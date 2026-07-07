#pragma once

#include "EditorCore/LSP/JsonRpc.h"
#include "EditorCore/LSP/LspProtocol.h"
#include "Tool/ProcessSession.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace VisualForge::EditorCore::LSP
{
    enum class LspClientState
    {
        Stopped,
        Starting,
        Running,
        Faulted
    };

    class LspClient final
    {
    public:
        void Start(std::wstring clangdPath);
        void Stop();

        void DidOpenFile(std::wstring const& uri, std::wstring const& languageId, std::wstring const& text);
        void DidChange(std::wstring const& uri, std::wstring const& text);
        void RequestCompletion(std::size_t line, std::size_t column);

        [[nodiscard]] std::vector<JsonRpcMessage> DrainReceivedMessages();
        [[nodiscard]] std::vector<LspProtocolMessage> DrainProtocolMessages();
        [[nodiscard]] LspClientState State() const noexcept;
        [[nodiscard]] std::vector<std::string> const& PendingMessages() const noexcept;
        [[nodiscard]] Tool::ProcessOutputSnapshot ProcessSnapshot() const;

    private:
        void SendJson(std::string message);
        [[nodiscard]] static std::string Narrow(std::wstring const& value);

        LspClientState m_state{ LspClientState::Stopped };
        std::wstring m_clangdPath;
        std::vector<std::string> m_pendingMessages;
        std::unique_ptr<Tool::ProcessSession> m_process;
        JsonRpcStreamParser m_parser;
        std::size_t m_stdoutBytesConsumed{ 0 };
    };
}
