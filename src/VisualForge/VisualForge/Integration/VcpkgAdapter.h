#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    class VcpkgAdapter final
    {
    public:
        explicit VcpkgAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateInstallCommand(std::filesystem::path const& manifestRoot, std::wstring triplet = L"x64-windows") const;
        [[nodiscard]] Tool::ToolCommand CreateListCommand(std::filesystem::path const& manifestRoot) const;

    private:
        Tool::ToolRegistry m_registry;
    };
}
