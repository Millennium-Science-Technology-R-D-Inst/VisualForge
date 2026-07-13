#include "pch.h"

#include "EditorCore/LSP/LspClient.h"

namespace VisualForge::EditorCore::LSP
{
    void LspClient::Start(
        std::wstring clangdPath,
        std::filesystem::path workingDirectory,
        std::filesystem::path compileCommandsDirectory)
    {
        Stop();
        m_clangdPath = std::move(clangdPath);
        m_workingDirectory = std::move(workingDirectory);
        m_compileCommandsDirectory = std::move(compileCommandsDirectory);
        m_documentVersion = 0;
        m_stdoutBytesConsumed = 0;
        m_nextRequestId = 10;
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
            if (!m_compileCommandsDirectory.empty())
            {
                command.Arguments.push_back(L"--compile-commands-dir=" + m_compileCommandsDirectory.wstring());
            }
            command.WorkingDirectory = m_workingDirectory;
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
            + R"(","version":)"
            + std::to_string(++m_documentVersion)
            + R"(,"text":")"
            + Narrow(text)
            + R"("}}})";
        SendJson(std::move(payload));
    }

    void LspClient::DidChange(std::wstring const& uri, std::wstring const& text)
    {
        auto payload = std::string{ R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")"
            + Narrow(uri)
            + R"(","version":)"
            + std::to_string(++m_documentVersion)
            + R"(},"contentChanges":[{"text":")"
            + Narrow(text)
            + R"("}]}})" };
        SendJson(std::move(payload));
    }

    int LspClient::RequestCompletion(std::wstring const& uri, std::size_t line, std::size_t column)
    {
        auto const requestId = m_nextRequestId++;
        auto payload = std::string{ R"({"jsonrpc":"2.0","id":)"
            + std::to_string(requestId)
            + R"(,"method":"textDocument/completion","params":{"textDocument":{"uri":")"
            + Narrow(uri)
            + R"("},"position":{"line":)"
            + std::to_string(line)
            + R"(,"character":)"
            + std::to_string(column)
            + R"(}}}})" };
        SendJson(std::move(payload));
        return requestId;
    }

    int LspClient::RequestHover(std::wstring const& uri, std::size_t line, std::size_t column)
    {
        auto const requestId = m_nextRequestId++;
        auto payload = std::string{ R"({"jsonrpc":"2.0","id":)"
            + std::to_string(requestId)
            + R"(,"method":"textDocument/hover","params":{"textDocument":{"uri":")"
            + Narrow(uri)
            + R"("},"position":{"line":)"
            + std::to_string(line)
            + R"(,"character":)"
            + std::to_string(column)
            + R"(}}}})" };
        SendJson(std::move(payload));
        return requestId;
    }

    int LspClient::RequestDefinition(std::wstring const& uri, std::size_t line, std::size_t column)
    {
        auto const requestId = m_nextRequestId++;
        auto payload = std::string{ R"({"jsonrpc":"2.0","id":)"
            + std::to_string(requestId)
            + R"(,"method":"textDocument/definition","params":{"textDocument":{"uri":")"
            + Narrow(uri)
            + R"("},"position":{"line":)"
            + std::to_string(line)
            + R"(,"character":)"
            + std::to_string(column)
            + R"(}}}})" };
        SendJson(std::move(payload));
        return requestId;
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
        if (value.empty())
        {
            return {};
        }

        auto const required = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            utf8.data(),
            required,
            nullptr,
            nullptr);

        std::string result;
        result.reserve(utf8.size() + 8);
        for (auto const character : utf8)
        {
            switch (static_cast<unsigned char>(character))
            {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                if (static_cast<unsigned char>(character) < 0x20)
                {
                    char buffer[7]{};
                    sprintf_s(buffer, "\\u%04x", static_cast<unsigned char>(character));
                    result += buffer;
                }
                else
                {
                    result.push_back(character);
                }
                break;
            }
        }

        return result;
    }
}
