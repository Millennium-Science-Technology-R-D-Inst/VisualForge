#include "pch.h"

#include "SolutionExplorerControl.h"

#if __has_include("UI/Xaml/View/Control/SolutionExplorerControl.g.cpp")
#include "UI/Xaml/View/Control/SolutionExplorerControl.g.cpp"
#endif

#include <algorithm>
#include <fstream>
#include <winrt/Windows.Data.Xml.Dom.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::VisualForge::UI::Xaml::View::Control::implementation
{
    SolutionExplorerControl::SolutionExplorerControl()
    {
    }

    void SolutionExplorerControl::LoadWorkspace(hstring const& rootPath, hstring const& solutionPath)
    {
        auto const rootPathValue = std::filesystem::path{ rootPath.c_str() };
        auto const solutionPathValue = std::filesystem::path{ solutionPath.c_str() };
        auto const solutionNode = TreeViewNode{};
        auto solutionName = solutionPathValue.empty() ? rootPathValue.filename().wstring() : solutionPathValue.stem().wstring();
        solutionNode.Content(box_value(hstring{ solutionName + L" (Solution)" }));
        solutionNode.IsExpanded(true);
        WorkspaceTree().RootNodes().Clear();
        m_fileNodes.clear();
        WorkspaceTree().RootNodes().Append(solutionNode);

        std::vector<std::filesystem::path> projects;
        if (!solutionPathValue.empty() && std::filesystem::exists(solutionPathValue))
        {
            std::ifstream stream{ solutionPathValue };
            std::string xml{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
            try
            {
                Windows::Data::Xml::Dom::XmlDocument document;
                document.LoadXml(to_hstring(xml));
                auto projectNodes = document.SelectNodes(L"//*[local-name()='Project']");
                for (uint32_t index = 0; index < projectNodes.Length(); ++index)
                {
                    auto projectNode = projectNodes.Item(index);
                    auto pathAttribute = projectNode.Attributes().GetNamedItem(L"Path");
                    if (pathAttribute && pathAttribute.NodeValue())
                    {
                        auto project = std::filesystem::path{ unbox_value<hstring>(pathAttribute.NodeValue()).c_str() };
                        projects.push_back(solutionPathValue.parent_path() / project);
                    }
                }
            }
            catch (hresult_error const&)
            {
                // Keep the workspace usable when an external solution contains invalid XML.
            }
        }

        if (projects.empty())
        {
            for (auto const& entry : std::filesystem::directory_iterator(rootPathValue))
            {
                if (entry.is_regular_file() && entry.path().extension() == L".vcxproj")
                {
                    projects.push_back(entry.path());
                }
            }
        }

        std::sort(projects.begin(), projects.end());
        for (auto const& project : projects)
        {
            if (std::filesystem::exists(project))
            {
                AddProjectNode(solutionNode, project);
            }
        }
    }

    event_token SolutionExplorerControl::FileOpenRequested(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, hstring> const& handler)
    {
        return m_fileOpenRequested.add(handler);
    }

    void SolutionExplorerControl::FileOpenRequested(event_token const& token) noexcept
    {
        m_fileOpenRequested.remove(token);
    }

    void SolutionExplorerControl::WorkspaceTree_SelectionChanged(TreeView const& sender, TreeViewSelectionChangedEventArgs const&)
    {
        auto node = sender.SelectedNode();
        if (!node)
        {
            return;
        }

        auto const found = m_fileNodes.find(winrt::get_abi(node));
        if (found == m_fileNodes.end())
        {
            return;
        }

        auto const path = hstring{ found->second.wstring() };
        if (m_fileOpenRequested)
        {
            m_fileOpenRequested(*this, path);
        }
    }

    void SolutionExplorerControl::AddProjectNode(TreeViewNode const& solutionNode, std::filesystem::path const& projectPath)
    {
        auto projectNode = TreeViewNode{};
        projectNode.Content(box_value(hstring{ projectPath.stem().wstring() }));
        projectNode.IsExpanded(true);
        solutionNode.Children().Append(projectNode);
        AddFolderNodes(projectNode, projectPath.parent_path(), 0);
    }

    void SolutionExplorerControl::AddFolderNodes(TreeViewNode const& parent, std::filesystem::path const& folder, int depth)
    {
        if (depth >= 10)
        {
            return;
        }

        std::error_code error;
        std::vector<std::filesystem::directory_entry> entries;
        for (auto const& entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, error))
        {
            if (error)
            {
                break;
            }
            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](auto const& left, auto const& right)
        {
            std::error_code leftError;
            std::error_code rightError;
            auto const leftDirectory = left.is_directory(leftError);
            auto const rightDirectory = right.is_directory(rightError);
            if (leftDirectory != rightDirectory)
            {
                return leftDirectory;
            }
            return left.path().filename().wstring() < right.path().filename().wstring();
        });

        for (auto const& entry : entries)
        {
            auto const name = entry.path().filename().wstring();
            if (entry.is_directory(error))
            {
                if (name == L".git" || name == L".vs" || name == L"obj" || name == L"x64" || name == L"vcpkg_installed")
                {
                    continue;
                }

                auto child = TreeViewNode{};
                child.Content(box_value(hstring{ name }));
                child.IsExpanded(depth < 1);
                parent.Children().Append(child);
                AddFolderNodes(child, entry.path(), depth + 1);
            }
            else if (entry.is_regular_file(error))
            {
                auto child = TreeViewNode{};
                child.Content(box_value(hstring{ name }));
                parent.Children().Append(child);
                m_fileNodes.emplace(winrt::get_abi(child), entry.path());
            }
        }
    }
}
