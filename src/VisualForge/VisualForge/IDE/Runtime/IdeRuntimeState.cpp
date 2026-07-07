#include "pch.h"

#include "IDE/Runtime/IdeRuntimeState.h"

#include <algorithm>
#include <sstream>

namespace VisualForge::IDE::Runtime
{
    namespace
    {
        std::wstring ToStateText(RuntimeSubsystemState state)
        {
            switch (state)
            {
            case RuntimeSubsystemState::Offline:
                return L"offline";
            case RuntimeSubsystemState::Starting:
                return L"starting";
            case RuntimeSubsystemState::Ready:
                return L"ready";
            case RuntimeSubsystemState::Busy:
                return L"busy";
            case RuntimeSubsystemState::Faulted:
                return L"faulted";
            default:
                return L"unknown";
            }
        }

        std::wstring SeverityText(EditorCore::LSP::LspDiagnosticSeverity severity)
        {
            switch (severity)
            {
            case EditorCore::LSP::LspDiagnosticSeverity::Error:
                return L"Error";
            case EditorCore::LSP::LspDiagnosticSeverity::Warning:
                return L"Warning";
            case EditorCore::LSP::LspDiagnosticSeverity::Information:
                return L"Info";
            case EditorCore::LSP::LspDiagnosticSeverity::Hint:
                return L"Hint";
            default:
                return L"Diagnostic";
            }
        }

        std::wstring FileNameFromUri(std::wstring const& uri)
        {
            constexpr std::wstring_view prefix{ L"file:///" };
            if (uri.starts_with(prefix))
            {
                auto path = uri.substr(prefix.size());
                std::replace(path.begin(), path.end(), L'/', L'\\');
                return std::filesystem::path{ path }.filename().wstring();
            }

            if (!uri.empty())
            {
                return std::filesystem::path{ uri }.filename().wstring();
            }

            return L"";
        }

        std::wstring LastPathSegment(std::filesystem::path const& path)
        {
            if (path.empty())
            {
                return L"";
            }

            return path.filename().wstring();
        }
    }

    void IdeRuntimeState::Reset()
    {
        m_languageService = {};
        m_build = {};
        m_debugger = {};
        m_output.clear();
        AppendOutput(L"VisualForge", L"IDE runtime initialized.");
    }

    void IdeRuntimeState::MarkLanguageServiceStarting()
    {
        m_languageService.State = RuntimeSubsystemState::Starting;
        m_languageService.LastMessage = L"starting clangd";
    }

    void IdeRuntimeState::MarkLanguageServiceStopped()
    {
        m_languageService.State = RuntimeSubsystemState::Offline;
        m_languageService.LastMessage = L"clangd stopped";
    }

    void IdeRuntimeState::MarkDebugStarting()
    {
        m_debugger.State = RuntimeSubsystemState::Starting;
        m_debugger.LastEvent = L"starting debug adapter";
        m_debugger.IsStopped = false;
    }

    void IdeRuntimeState::MarkDebugStopped()
    {
        m_debugger.State = RuntimeSubsystemState::Offline;
        m_debugger.LastEvent = L"debug adapter stopped";
        m_debugger.IsStopped = false;
    }

    void IdeRuntimeState::ApplyMSBuildEvaluation(Integration::ProjectContext const& context, Integration::MSBuildEvaluationResult const& result)
    {
        m_build.HasEvaluation = true;
        m_build.Completed = result.Completed;
        m_build.ExitCode = result.ExitCode;
        m_build.EvaluationXmlPath = result.EvaluationXmlPath;
        m_build.CompileCommandsPath = result.CompileCommandsPath;
        m_build.State = !result.Started || (result.Completed && result.ExitCode != 0)
            ? RuntimeSubsystemState::Faulted
            : result.Completed ? RuntimeSubsystemState::Ready : RuntimeSubsystemState::Busy;

        auto projectName = LastPathSegment(context.ProjectPath.empty() ? context.SolutionPath : context.ProjectPath);
        std::wstringstream line;
        line << L"MSBuild evaluation " << ToStateText(m_build.State);
        if (!projectName.empty())
        {
            line << L" for " << projectName;
        }

        if (!m_build.CompileCommandsPath.empty())
        {
            line << L"; compile database: " << m_build.CompileCommandsPath.filename().wstring();
        }

        AppendOutput(L"MSBuild", line.str());
    }

    void IdeRuntimeState::ApplyLspMessages(std::vector<EditorCore::LSP::LspProtocolMessage> const& messages)
    {
        if (messages.empty())
        {
            return;
        }

        m_languageService.State = RuntimeSubsystemState::Ready;
        for (auto const& message : messages)
        {
            if (!message.Summary.Method.empty())
            {
                m_languageService.LastMessage = message.Summary.Method;
            }

            if (!message.Summary.Command.empty())
            {
                m_languageService.LastMessage = message.Summary.Command;
            }

            if (!message.Diagnostics.empty())
            {
                auto const uri = message.Diagnostics.front().Uri;
                auto const file = FileNameFromUri(uri);
                m_languageService.Diagnostics.erase(
                    std::remove_if(m_languageService.Diagnostics.begin(), m_languageService.Diagnostics.end(), [&](ProblemItem const& item)
                    {
                        return item.File == file;
                    }),
                    m_languageService.Diagnostics.end());

                for (auto const& diagnostic : message.Diagnostics)
                {
                    m_languageService.Diagnostics.push_back({
                        SeverityText(diagnostic.Severity),
                        L"clangd",
                        diagnostic.Message,
                        L"Language Service",
                        FileNameFromUri(diagnostic.Uri),
                        0
                    });
                }
            }
        }

        RecountDiagnostics();
        AppendOutput(L"clangd", m_languageService.LastMessage.empty() ? L"protocol messages received" : m_languageService.LastMessage);
    }

    void IdeRuntimeState::ApplyDapMessages(std::vector<EditorCore::DAP::DapProtocolMessage> const& messages)
    {
        if (messages.empty())
        {
            return;
        }

        m_debugger.State = RuntimeSubsystemState::Ready;
        for (auto const& message : messages)
        {
            if (!message.Summary.Event.empty())
            {
                m_debugger.LastEvent = message.Summary.Event;
                m_debugger.IsStopped = message.Summary.Event == L"stopped";
                if (message.Summary.Event == L"continued")
                {
                    m_debugger.IsStopped = false;
                }
            }

            if (!message.Summary.Command.empty())
            {
                m_debugger.LastCommand = message.Summary.Command;
            }

            if (message.Summary.Success && !*message.Summary.Success)
            {
                m_debugger.State = RuntimeSubsystemState::Faulted;
            }
        }

        AppendOutput(L"DAP", m_debugger.LastEvent.empty() ? m_debugger.LastCommand : m_debugger.LastEvent);
    }

    void IdeRuntimeState::AppendOutput(std::wstring source, std::wstring text)
    {
        if (text.empty())
        {
            return;
        }

        m_output.push_back({ std::move(source), std::move(text) });
        constexpr std::size_t maxOutputEntries = 200;
        if (m_output.size() > maxOutputEntries)
        {
            m_output.erase(m_output.begin(), m_output.begin() + static_cast<std::ptrdiff_t>(m_output.size() - maxOutputEntries));
        }
    }

    LanguageServiceRuntimeState const& IdeRuntimeState::LanguageService() const noexcept
    {
        return m_languageService;
    }

    BuildRuntimeState const& IdeRuntimeState::Build() const noexcept
    {
        return m_build;
    }

    DebugRuntimeState const& IdeRuntimeState::Debugger() const noexcept
    {
        return m_debugger;
    }

    std::vector<OutputEntry> const& IdeRuntimeState::Output() const noexcept
    {
        return m_output;
    }

    std::wstring IdeRuntimeState::LanguageServiceStatusText() const
    {
        auto text = L"clangd: " + ToStateText(m_languageService.State);
        if (!m_languageService.LastMessage.empty())
        {
            text += L" (" + m_languageService.LastMessage + L")";
        }

        return text;
    }

    std::wstring IdeRuntimeState::BuildStatusText() const
    {
        if (!m_build.HasEvaluation)
        {
            return L"MSBuild: not evaluated";
        }

        auto text = L"MSBuild: " + ToStateText(m_build.State);
        if (!m_build.CompileCommandsPath.empty())
        {
            text += L" + compile_commands";
        }

        return text;
    }

    std::wstring IdeRuntimeState::DebugStatusText() const
    {
        auto text = L"DAP: " + ToStateText(m_debugger.State);
        if (!m_debugger.LastEvent.empty())
        {
            text += L" (" + m_debugger.LastEvent + L")";
        }

        return text;
    }

    std::wstring IdeRuntimeState::ProblemsStatusText() const
    {
        std::wstringstream text;
        text << m_languageService.ErrorCount << L" Errors, " << m_languageService.WarningCount << L" Warnings";
        return text.str();
    }

    std::wstring IdeRuntimeState::OutputPreviewText(std::size_t maxEntries) const
    {
        std::wstringstream text;
        auto const start = m_output.size() > maxEntries ? m_output.size() - maxEntries : 0;
        for (auto index = start; index < m_output.size(); ++index)
        {
            if (index > start)
            {
                text << L"\n";
            }

            text << L"[" << m_output[index].Source << L"] " << m_output[index].Text;
        }

        return text.str();
    }

    void IdeRuntimeState::RecountDiagnostics()
    {
        m_languageService.ErrorCount = 0;
        m_languageService.WarningCount = 0;
        m_languageService.InformationCount = 0;
        m_languageService.HintCount = 0;

        for (auto const& item : m_languageService.Diagnostics)
        {
            if (item.Severity == L"Error")
            {
                ++m_languageService.ErrorCount;
            }
            else if (item.Severity == L"Warning")
            {
                ++m_languageService.WarningCount;
            }
            else if (item.Severity == L"Info")
            {
                ++m_languageService.InformationCount;
            }
            else if (item.Severity == L"Hint")
            {
                ++m_languageService.HintCount;
            }
        }
    }
}
