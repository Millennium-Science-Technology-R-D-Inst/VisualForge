#include "pch.h"

#include "Integration/GitAdapter.h"

namespace VisualForge::Integration
{
    GitAdapter::GitAdapter(Tool::ToolRegistry registry) :
        m_registry(std::move(registry))
    {
    }

    AdapterSnapshot GitAdapter::Describe() const
    {
        return {
            L"GitAdapter",
            L"Repository status, diff, and history command planner.",
            AdapterStatus::Ready,
            {
                { L"Status", L"Reads porcelain status for Project Explorer badges." },
                { L"Diff", L"Builds file or repository diff commands." },
                { L"History", L"Builds compact commit log commands for the Git UI." }
            }
        };
    }

    Tool::ToolCommand GitAdapter::CreateStatusCommand(std::filesystem::path const& repositoryRoot) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"status", L"--short", L"--branch" },
            repositoryRoot,
            L"Read Git status");
    }

    Tool::ToolCommand GitAdapter::CreateDiffCommand(std::filesystem::path const& repositoryRoot, std::filesystem::path const& path) const
    {
        std::vector<std::wstring> arguments{ L"diff", L"--" };
        if (!path.empty())
        {
            arguments.push_back(path.wstring());
        }

        return m_registry.CreateCommand(Tool::ToolKind::Git, std::move(arguments), repositoryRoot, L"Read Git diff");
    }

    Tool::ToolCommand GitAdapter::CreateLogCommand(std::filesystem::path const& repositoryRoot, int maxCount) const
    {
        if (maxCount <= 0)
        {
            maxCount = 30;
        }

        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"log", L"--oneline", L"--decorate", L"--max-count=" + std::to_wstring(maxCount) },
            repositoryRoot,
            L"Read Git history");
    }
}
