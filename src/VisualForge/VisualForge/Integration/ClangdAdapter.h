#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    class ClangdAdapter final
    {
    public:
        explicit ClangdAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateStartCommand(ProjectContext const& context) const;
        [[nodiscard]] Tool::ToolCommand CreateCheckCommand(ProjectContext const& context, std::filesystem::path const& sourceFile) const;

    private:
        Tool::ToolRegistry m_registry;
    };
}
