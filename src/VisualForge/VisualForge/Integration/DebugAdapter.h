#pragma once

#include "Integration/AdapterContracts.h"
#include "Tool/ToolRegistry.h"

namespace VisualForge::Integration
{
    struct DebugLaunchProfile
    {
        std::filesystem::path Program;
        std::wstring Arguments;
        std::filesystem::path WorkingDirectory;
        bool StopAtEntry{ false };
    };

    class DebugAdapter final
    {
    public:
        explicit DebugAdapter(Tool::ToolRegistry registry = Tool::ToolRegistry::CreateDefault());

        [[nodiscard]] AdapterSnapshot Describe() const;
        [[nodiscard]] Tool::ToolCommand CreateDebugAdapterCommand(std::filesystem::path const& workingDirectory) const;
        [[nodiscard]] std::wstring CreateLaunchRequest(DebugLaunchProfile const& profile) const;

    private:
        [[nodiscard]] static std::wstring JsonEscape(std::wstring_view value);

        Tool::ToolRegistry m_registry;
    };
}
