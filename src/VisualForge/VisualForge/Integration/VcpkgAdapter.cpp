#include "pch.h"

#include "Integration/VcpkgAdapter.h"

namespace VisualForge::Integration
{
    VcpkgAdapter::VcpkgAdapter(Tool::ToolRegistry registry) :
        m_registry(std::move(registry))
    {
    }

    AdapterSnapshot VcpkgAdapter::Describe() const
    {
        return {
            L"VcpkgAdapter",
            L"Manifest-mode C/C++ package manager integration.",
            AdapterStatus::Ready,
            {
                { L"Install", L"Builds vcpkg install commands for a manifest root." },
                { L"List", L"Reads installed package inventory." },
                { L"Triplets", L"Carries the selected binary triplet in the command model." }
            }
        };
    }

    Tool::ToolCommand VcpkgAdapter::CreateInstallCommand(std::filesystem::path const& manifestRoot, std::wstring triplet) const
    {
        if (triplet.empty())
        {
            triplet = L"x64-windows";
        }

        return m_registry.CreateCommand(
            Tool::ToolKind::Vcpkg,
            { L"install", L"--x-manifest-root=" + manifestRoot.wstring(), L"--triplet", std::move(triplet) },
            manifestRoot,
            L"Install vcpkg packages");
    }

    Tool::ToolCommand VcpkgAdapter::CreateListCommand(std::filesystem::path const& manifestRoot) const
    {
        return m_registry.CreateCommand(Tool::ToolKind::Vcpkg, { L"list" }, manifestRoot, L"List vcpkg packages");
    }
}
