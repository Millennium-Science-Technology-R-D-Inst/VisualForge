#include "pch.h"

#include "Integration/DebugAdapter.h"

namespace VisualForge::Integration
{
    DebugAdapter::DebugAdapter(Tool::ToolRegistry registry) :
        m_registry(std::move(registry))
    {
    }

    AdapterSnapshot DebugAdapter::Describe() const
    {
        return {
            L"DebugAdapter",
            L"DAP command and launch-request planner backed by LLDB.",
            AdapterStatus::RequiresProject,
            {
                { L"DAP", L"Starts lldb-dap for launch and breakpoint workflows." },
                { L"Launch", L"Builds launch request payloads for native executables." },
                { L"Roadmap", L"Leaves room for PDB, NatVis, attach, and dump debugging." }
            }
        };
    }

    Tool::ToolCommand DebugAdapter::CreateDebugAdapterCommand(std::filesystem::path const& workingDirectory) const
    {
        return m_registry.CreateCommand(Tool::ToolKind::LLDB, {}, workingDirectory, L"Start LLDB DAP");
    }

    std::wstring DebugAdapter::CreateLaunchRequest(DebugLaunchProfile const& profile) const
    {
        std::wstring request = L"{";
        request += L"\"type\":\"lldb\",";
        request += L"\"request\":\"launch\",";
        request += L"\"name\":\"VisualForge Launch\",";
        request += L"\"program\":\"" + JsonEscape(profile.Program.wstring()) + L"\",";
        request += L"\"args\":\"" + JsonEscape(profile.Arguments) + L"\",";
        request += L"\"cwd\":\"" + JsonEscape(profile.WorkingDirectory.wstring()) + L"\",";
        request += L"\"stopOnEntry\":";
        request += profile.StopAtEntry ? L"true" : L"false";
        request += L"}";
        return request;
    }

    EditorCore::DAP::DapClient DebugAdapter::CreateClient(
        std::filesystem::path const& lldbDapPath,
        std::filesystem::path const& workingDirectory) const
    {
        EditorCore::DAP::DapClient client;
        auto const started = client.Start(lldbDapPath.wstring(), workingDirectory);
        (void)started;
        return client;
    }

    std::wstring DebugAdapter::JsonEscape(std::wstring_view value)
    {
        std::wstring escaped;
        escaped.reserve(value.size());

        for (wchar_t current : value)
        {
            switch (current)
            {
            case L'\\':
                escaped += L"\\\\";
                break;
            case L'"':
                escaped += L"\\\"";
                break;
            case L'\n':
                escaped += L"\\n";
                break;
            case L'\r':
                escaped += L"\\r";
                break;
            case L'\t':
                escaped += L"\\t";
                break;
            default:
                escaped.push_back(current);
                break;
            }
        }

        return escaped;
    }
}
