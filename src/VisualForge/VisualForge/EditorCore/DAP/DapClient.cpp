#include "pch.h"

#include "EditorCore/DAP/DapClient.h"

#include <sstream>

namespace VisualForge::EditorCore::DAP
{
    bool DapClient::Start(std::wstring lldbDapPath, std::filesystem::path workingDirectory)
    {
        m_nextSequence = 1;
        m_sentMessages.clear();
        m_stdoutBytesConsumed = 0;
        m_parser.Clear();
        Tool::ToolCommand command;
        command.Kind = Tool::ToolKind::LLDB;
        command.Executable = std::move(lldbDapPath);
        command.WorkingDirectory = std::move(workingDirectory);
        command.DisplayName = L"LLDB Debug Adapter";

        m_process = std::make_unique<Tool::ProcessSession>();
        if (!m_process->Start(command))
        {
            m_process.reset();
            m_state = DapClientState::Faulted;
            return false;
        }

        m_state = DapClientState::Running;
        Initialize();
        return true;
    }

    void DapClient::Stop()
    {
        if (m_process)
        {
            Disconnect();
            m_process->Stop(0);
            m_process.reset();
        }

        m_state = DapClientState::Stopped;
    }

    void DapClient::Initialize()
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"initialize","arguments":{"clientID":"visualforge","clientName":"VisualForge","adapterID":"lldb","pathFormat":"path","linesStartAt1":true,"columnsStartAt1":true,"supportsVariableType":true}})");
    }

    void DapClient::Launch(std::wstring const& program, std::wstring const& arguments, std::wstring const& cwd, bool stopAtEntry)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence())
             + R"(,"type":"request","command":"launch","arguments":{"program":")" + Narrow(program)
             + R"(","args":)" + (arguments.empty() ? "[]" : Narrow(arguments))
             + R"(","cwd":")" + Narrow(cwd)
             + R"(","stopOnEntry":)" + (stopAtEntry ? "true" : "false")
             + R"(}})");
    }

    void DapClient::SetBreakpoints(std::wstring const& sourcePath, std::vector<int> const& lines)
    {
        std::ostringstream breakpoints;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (index > 0)
            {
                breakpoints << ',';
            }

            breakpoints << R"({"line":)" << lines[index] << '}';
        }

        Send(R"({"seq":)" + std::to_string(NextSequence())
             + R"(,"type":"request","command":"setBreakpoints","arguments":{"source":{"path":")" + Narrow(sourcePath)
             + R"("},"breakpoints":[)" + breakpoints.str()
             + R"(]}})");
    }

    void DapClient::Continue(int threadId)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"continue","arguments":{"threadId":)" + std::to_string(threadId) + R"(}})");
    }

    void DapClient::Next(int threadId)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"next","arguments":{"threadId":)" + std::to_string(threadId) + R"(}})");
    }

    void DapClient::StepIn(int threadId)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"stepIn","arguments":{"threadId":)" + std::to_string(threadId) + R"(}})");
    }

    void DapClient::StepOut(int threadId)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"stepOut","arguments":{"threadId":)" + std::to_string(threadId) + R"(}})");
    }

    void DapClient::RequestStackTrace(int threadId)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"stackTrace","arguments":{"threadId":)" + std::to_string(threadId) + R"(}})");
    }

    void DapClient::RequestScopes(int frameId)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"scopes","arguments":{"frameId":)" + std::to_string(frameId) + R"(}})");
    }

    void DapClient::RequestVariables(int variablesReference)
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"variables","arguments":{"variablesReference":)" + std::to_string(variablesReference) + R"(}})");
    }

    void DapClient::ConfigurationDone()
    {
        Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"configurationDone","arguments":{}})");
    }

    void DapClient::Disconnect()
    {
        if (m_state == DapClientState::Running)
        {
            Send(R"({"seq":)" + std::to_string(NextSequence()) + R"(,"type":"request","command":"disconnect","arguments":{"terminateDebuggee":true}})");
        }
    }

    std::vector<LSP::JsonRpcMessage> DapClient::DrainReceivedMessages()
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

    std::vector<DapProtocolMessage> DapClient::DrainProtocolMessages()
    {
        auto messages = DrainReceivedMessages();
        std::vector<DapProtocolMessage> parsed;
        parsed.reserve(messages.size());
        for (auto const& message : messages)
        {
            parsed.push_back(DapProtocolParser::Parse(message));
        }

        return parsed;
    }

    DapClientState DapClient::State() const noexcept
    {
        return m_state;
    }

    std::vector<std::string> const& DapClient::SentMessages() const noexcept
    {
        return m_sentMessages;
    }

    Tool::ProcessOutputSnapshot DapClient::ProcessSnapshot() const
    {
        return m_process ? m_process->Snapshot() : Tool::ProcessOutputSnapshot{};
    }

    void DapClient::Send(std::string payload)
    {
        auto framed = LSP::JsonRpcFramer::Frame(payload);
        if (m_process && m_process->IsRunning())
        {
            auto const written = m_process->Write(framed);
            (void)written;
        }

        m_sentMessages.push_back(std::move(framed));
    }

    int DapClient::NextSequence() noexcept
    {
        return m_nextSequence++;
    }

    std::string DapClient::Narrow(std::wstring const& value)
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
