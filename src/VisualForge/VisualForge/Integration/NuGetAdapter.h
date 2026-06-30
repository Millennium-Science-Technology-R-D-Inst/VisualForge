#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    class NuGetAdapter final
    {
    public:
        explicit NuGetAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateRestoreCommand(std::filesystem::path const& solutionOrProjectPath) const;
        [[nodiscard]] Tool::ToolCommand CreateSourcesCommand(std::filesystem::path const& workingDirectory) const;

    private:
        Tool::ToolRegistry m_registry;
    };
}
