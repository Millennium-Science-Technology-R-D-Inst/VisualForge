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

    Tool::ToolCommand GitAdapter::CreateAddAllCommand(std::filesystem::path const& repositoryRoot) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"add", L"--all" },
            repositoryRoot,
            L"Stage all Git changes");
    }

    Tool::ToolCommand GitAdapter::CreateCommitCommand(std::filesystem::path const& repositoryRoot, std::wstring message) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"commit", L"-m", std::move(message) },
            repositoryRoot,
            L"Commit Git changes");
    }

    Tool::ToolCommand GitAdapter::CreatePullCommand(std::filesystem::path const& repositoryRoot) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"pull" },
            repositoryRoot,
            L"Pull Git changes");
    }

    Tool::ToolCommand GitAdapter::CreatePushCommand(std::filesystem::path const& repositoryRoot) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"push" },
            repositoryRoot,
            L"Push Git changes");
    }

    Tool::ToolCommand GitAdapter::CreateBranchListCommand(std::filesystem::path const& repositoryRoot) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"branch", L"--all", L"--no-color" },
            repositoryRoot,
            L"List Git branches");
    }

    Tool::ToolCommand GitAdapter::CreateCheckoutCommand(std::filesystem::path const& repositoryRoot, std::wstring branch) const
    {
        return m_registry.CreateCommand(
            Tool::ToolKind::Git,
            { L"checkout", std::move(branch) },
            repositoryRoot,
            L"Checkout Git branch");
    }
}
