#include "pch.h"

#include "EditorCore/LSP/LspClient.h"

namespace VisualForge::EditorCore::LSP
{
    void LspClient::Start(std::wstring clangdPath)
    {
        m_clangdPath = std::move(clangdPath);
        m_state = m_clangdPath.empty() ? LspClientState::Faulted : LspClientState::Starting;
        if (m_state == LspClientState::Starting)
        {
            Tool::ToolCommand command;
            command.Kind = Tool::ToolKind::Clangd;
            command.Executable = m_clangdPath;
            command.Arguments = {
                L"--background-index",
                L"--clang-tidy",
                L"--completion-style=detailed"
            };
            command.DisplayName = L"clangd language server";

            m_process = std::make_unique<Tool::ProcessSession>();
            if (!m_process->Start(command))
            {
                m_process.reset();
                m_state = LspClientState::Faulted;
                return;
            }

            SendJson(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})");
            SendJson(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
            m_state = LspClientState::Running;
        }
    }

    void LspClient::Stop()
    {
        if (m_state == LspClientState::Running)
        {
            SendJson(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})");
        }

        if (m_process)
        {
            m_process->Stop(0);
            m_process.reset();
        }

        m_state = LspClientState::Stopped;
    }

    void LspClient::DidOpenFile(std::wstring const& uri, std::wstring const& languageId, std::wstring const& text)
    {
        auto payload = std::string{ R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")" }
            + Narrow(uri)
            + R"(","languageId":")"
            + Narrow(languageId)
            + R"(","version":1,"text":")"
            + Narrow(text)
            + R"("}}})";
        SendJson(std::move(payload));
    }

    void LspClient::DidChange(std::wstring const& uri, std::wstring const& text)
    {
        auto payload = std::string{ R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")" }
            + Narrow(uri)
            + R"(","version":2},"contentChanges":[{"text":")"
            + Narrow(text)
            + R"("}]}})";
        SendJson(std::move(payload));
    }

    void LspClient::RequestCompletion(std::size_t line, std::size_t column)
    {
        auto payload = std::string{ R"({"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"position":{"line":)" }
            + std::to_string(line)
            + R"(,"character":)"
            + std::to_string(column)
            + R"(}}})";
        SendJson(std::move(payload));
    }

    std::vector<JsonRpcMessage> LspClient::DrainReceivedMessages()
    {
        if (!m_process)
        {
            return {};
        }

        auto snapshot = m_process->Snapshot();
        if (snapshot.StdOut.size() <= m_stdoutBytesConsumed)
        {
            return {};
        }

        auto const unread = std::string_view{ snapshot.StdOut }.substr(m_stdoutBytesConsumed);
        m_stdoutBytesConsumed = snapshot.StdOut.size();
        return m_parser.Append(unread);
    }

    std::vector<LspProtocolMessage> LspClient::DrainProtocolMessages()
    {
        auto messages = DrainReceivedMessages();
        std::vector<LspProtocolMessage> parsed;
        parsed.reserve(messages.size());
        for (auto const& message : messages)
        {
            parsed.push_back(LspProtocolParser::Parse(message));
        }

        return parsed;
    }

    LspClientState LspClient::State() const noexcept
    {
        return m_state;
    }

    std::vector<std::string> const& LspClient::PendingMessages() const noexcept
    {
        return m_pendingMessages;
    }

    Tool::ProcessOutputSnapshot LspClient::ProcessSnapshot() const
    {
        return m_process ? m_process->Snapshot() : Tool::ProcessOutputSnapshot{};
    }

    void LspClient::SendJson(std::string message)
    {
        auto framed = JsonRpcFramer::Frame(message);
        if (m_process && m_process->IsRunning())
        {
            auto const written = m_process->Write(framed);
            (void)written;
        }

        m_pendingMessages.push_back(std::move(framed));
    }

    std::string LspClient::Narrow(std::wstring const& value)
    {
        std::string result;
        result.reserve(value.size());
        for (auto character : value)
        {
            if (character == L'\\')
            {
                result += "\\\\";
            }
            else if (character == L'"')
            {
                result += "\\\"";
            }
            else if (character < 0x80)
            {
                result.push_back(static_cast<char>(character));
            }
            else
            {
                result.push_back('?');
            }
        }

        return result;
    }
}
