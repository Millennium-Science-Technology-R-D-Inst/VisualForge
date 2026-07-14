#pragma once

#include "EditorCore/LSP/JsonRpc.h"
#include "EditorCore/LSP/LspProtocol.h"
#include "Tool/ProcessSession.h"

#include <cstddef>
#include <filesystem>
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
        void Start(
            std::wstring clangdPath,
            std::filesystem::path workingDirectory = {},
            std::filesystem::path compileCommandsDirectory = {});
        void Stop();

        void DidOpenFile(std::wstring const& uri, std::wstring const& languageId, std::wstring const& text);
        void DidCloseFile(std::wstring const& uri);
        void DidChange(std::wstring const& uri, std::wstring const& text);
        int RequestCompletion(std::wstring const& uri, std::size_t line, std::size_t column);
        int RequestHover(std::wstring const& uri, std::size_t line, std::size_t column);
        int RequestDefinition(std::wstring const& uri, std::size_t line, std::size_t column);
        int RequestReferences(std::wstring const& uri, std::size_t line, std::size_t column);
        int RequestRename(std::wstring const& uri, std::size_t line, std::size_t column, std::wstring const& newName);
        int RequestSemanticTokens(std::wstring const& uri);
        int RequestFormatting(std::wstring const& uri, std::size_t tabSize = 4, bool insertSpaces = true);
        int RequestCodeActions(std::wstring const& uri, std::size_t line, std::size_t column,
            std::size_t endLine, std::size_t endColumn);
        int RequestDocumentSymbols(std::wstring const& uri);

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
        std::filesystem::path m_workingDirectory;
        std::filesystem::path m_compileCommandsDirectory;
        std::vector<std::string> m_pendingMessages;
        std::unique_ptr<Tool::ProcessSession> m_process;
        JsonRpcStreamParser m_parser;
        std::size_t m_stdoutBytesConsumed{ 0 };
        std::size_t m_documentVersion{ 0 };
        int m_nextRequestId{ 10 };
    };
}
