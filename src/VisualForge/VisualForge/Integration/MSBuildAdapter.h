#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ProcessSession.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    struct MSBuildEvaluationResult
    {
        bool Started{ false };
        bool Completed{ false };
        unsigned long ExitCode{ 0 };
        std::filesystem::path EvaluationXmlPath;
        std::filesystem::path CompileCommandsPath;
        std::string Output;
        std::string Error;
    };

    class MSBuildAdapter final
    {
    public:
        explicit MSBuildAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateRestoreCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateBuildCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateRebuildCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateCleanCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateGenerateCompileCommandsCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateEvaluationCommand(ProjectContext const& context, std::filesystem::path outputPath = {}) const;
        [[nodiscard]] CommandPlan CreateLanguageServicePreparationPlan(ProjectContext const& context) const;
        [[nodiscard]] MSBuildEvaluationResult EvaluateProject(ProjectContext const& context, unsigned long timeoutMs = 30000) const;

    private:
        [[nodiscard]] Tool::ToolCommand CreateTargetCommand(ProjectContext const& context,
            std::wstring target, std::wstring description) const;
        [[nodiscard]] static std::filesystem::path SelectBuildPath(ProjectContext const& context);
        [[nodiscard]] static std::vector<std::wstring> CreateConfigurationProperties(ProjectContext const& context);

        Tool::ToolRegistry m_registry;
    };
}
