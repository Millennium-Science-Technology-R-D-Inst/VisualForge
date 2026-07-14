#pragma once

#include "EditorCore/DAP/DapProtocol.h"
#include "EditorCore/LSP/LspProtocol.h"
#include "Integration/MSBuildAdapter.h"

#include <filesystem>
#include <string>
#include <vector>

namespace VisualForge::IDE::Runtime
{
    enum class RuntimeSubsystemState
    {
        Offline,
        Starting,
        Ready,
        Busy,
        Faulted
    };

    struct ProblemItem
    {
        std::wstring Severity;
        std::wstring Code;
        std::wstring Description;
        std::wstring Project;
        std::wstring File;
        int Line{ 0 };
    };

    struct OutputEntry
    {
        std::wstring Source;
        std::wstring Text;
    };

    struct LanguageServiceRuntimeState
    {
        RuntimeSubsystemState State{ RuntimeSubsystemState::Offline };
        std::wstring LastMessage;
        std::size_t ErrorCount{ 0 };
        std::size_t WarningCount{ 0 };
        std::size_t InformationCount{ 0 };
        std::size_t HintCount{ 0 };
        std::vector<ProblemItem> Diagnostics;
    };

    struct BuildRuntimeState
    {
        RuntimeSubsystemState State{ RuntimeSubsystemState::Offline };
        bool HasEvaluation{ false };
        bool Completed{ false };
        unsigned long ExitCode{ 0 };
        std::filesystem::path EvaluationXmlPath;
        std::filesystem::path CompileCommandsPath;
    };

    struct DebugRuntimeState
    {
        RuntimeSubsystemState State{ RuntimeSubsystemState::Offline };
        std::wstring LastEvent;
        std::wstring LastCommand;
        bool IsStopped{ false };
    };

    class IdeRuntimeState final
    {
    public:
        void Reset();
        void MarkLanguageServiceStarting();
        void MarkLanguageServiceReady();
        void MarkLanguageServiceStopped();
        void MarkDebugStarting();
        void MarkDebugStopped();
        void ApplyMSBuildEvaluation(Integration::ProjectContext const& context, Integration::MSBuildEvaluationResult const& result);
        void ApplyLspMessages(std::vector<EditorCore::LSP::LspProtocolMessage> const& messages);
        void ApplyDapMessages(std::vector<EditorCore::DAP::DapProtocolMessage> const& messages);
        void AppendOutput(std::wstring source, std::wstring text);

        [[nodiscard]] LanguageServiceRuntimeState const& LanguageService() const noexcept;
        [[nodiscard]] BuildRuntimeState const& Build() const noexcept;
        [[nodiscard]] DebugRuntimeState const& Debugger() const noexcept;
        [[nodiscard]] std::vector<OutputEntry> const& Output() const noexcept;
        [[nodiscard]] std::wstring LanguageServiceStatusText() const;
        [[nodiscard]] std::wstring BuildStatusText() const;
        [[nodiscard]] std::wstring DebugStatusText() const;
        [[nodiscard]] std::wstring ProblemsStatusText() const;
        [[nodiscard]] std::wstring OutputPreviewText(std::size_t maxEntries = 8) const;

    private:
        void RecountDiagnostics();

        LanguageServiceRuntimeState m_languageService;
        BuildRuntimeState m_build;
        DebugRuntimeState m_debugger;
        std::vector<OutputEntry> m_output;
    };
}
