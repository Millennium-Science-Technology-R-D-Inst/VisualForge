#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    class GitAdapter final
    {
    public:
        explicit GitAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateStatusCommand(std::filesystem::path const& repositoryRoot) const;
        [[nodiscard]] Tool::ToolCommand CreateDiffCommand(std::filesystem::path const& repositoryRoot, std::filesystem::path const& path = {}) const;
        [[nodiscard]] Tool::ToolCommand CreateLogCommand(std::filesystem::path const& repositoryRoot, int maxCount = 30) const;

    private:
        Tool::ToolRegistry m_registry;
    };
}
