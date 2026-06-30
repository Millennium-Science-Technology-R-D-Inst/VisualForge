#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    class MSBuildAdapter final
    {
    public:
        explicit MSBuildAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateRestoreCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateBuildCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateGenerateCompileCommandsCommand(ProjectContext const& context) const;
        [[nodiscard]] CommandPlan CreateLanguageServicePreparationPlan(ProjectContext const& context) const;

    private:
        [[nodiscard]] static std::filesystem::path SelectBuildPath(ProjectContext const& context);
        [[nodiscard]] static std::vector<std::wstring> CreateConfigurationProperties(ProjectContext const& context);

        Tool::ToolRegistry m_registry;
    };
}
