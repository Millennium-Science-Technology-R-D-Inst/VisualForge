#include "pch.h"

#include "SolutionExplorerControl.h"

#if __has_include("UI/Xaml/View/Control/SolutionExplorerControl.g.cpp")
#include "UI/Xaml/View/Control/SolutionExplorerControl.g.cpp"
#endif

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <winrt/Windows.Data.Xml.Dom.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::VisualForge::UI::Xaml::View::Control::implementation
{
    namespace
    {
        winrt::hstring ReadXmlText(std::filesystem::path const& path)
        {
            std::ifstream stream{ path, std::ios::binary };
            if (!stream)
            {
                return {};
            }
            std::string xml{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
            if (xml.size() >= 3 && static_cast<unsigned char>(xml[0]) == 0xEF
                && static_cast<unsigned char>(xml[1]) == 0xBB
                && static_cast<unsigned char>(xml[2]) == 0xBF)
            {
                xml.erase(0, 3);
            }
            return winrt::to_hstring(xml);
        }
    }

    SolutionExplorerControl::SolutionExplorerControl()
    {
    }

    void SolutionExplorerControl::LoadWorkspace(hstring const& rootPath, hstring const& solutionPath)
    {
        auto const rootPathValue = std::filesystem::path{ rootPath.c_str() };
        m_workspaceRoot = rootPathValue;
        auto const solutionPathValue = std::filesystem::path{ solutionPath.c_str() };
        m_solutionPath = solutionPathValue;
        auto const solutionNode = TreeViewNode{};
        auto solutionName = solutionPathValue.empty() ? rootPathValue.filename().wstring() : solutionPathValue.stem().wstring();
        solutionNode.Content(box_value(hstring{ solutionName + L" (Solution)" }));
        solutionNode.IsExpanded(true);
        WorkspaceTree().RootNodes().Clear();
        m_fileNodes.clear();
        m_projectNodes.clear();
        WorkspaceTree().RootNodes().Append(solutionNode);

        std::vector<std::filesystem::path> projects;
        if (!solutionPathValue.empty() && std::filesystem::exists(solutionPathValue))
        {
            try
            {
                Windows::Data::Xml::Dom::XmlDocument document;
                document.LoadXml(ReadXmlText(solutionPathValue));
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

        if (projects.empty() && m_showAllFiles)
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

    event_token SolutionExplorerControl::ProjectSelectedRequested(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, hstring> const& handler)
    {
        return m_projectSelectedRequested.add(handler);
    }

    void SolutionExplorerControl::ProjectSelectedRequested(event_token const& token) noexcept
    {
        m_projectSelectedRequested.remove(token);
    }

    event_token SolutionExplorerControl::ContextActionRequested(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, hstring> const& handler)
    {
        return m_contextActionRequested.add(handler);
    }

    void SolutionExplorerControl::ContextActionRequested(event_token const& token) noexcept
    {
        m_contextActionRequested.remove(token);
    }

    void SolutionExplorerControl::SetStartupProject(hstring const& projectPath)
    {
        m_startupProject = std::filesystem::path{ projectPath.c_str() };
        for (auto const& [_, project] : m_projectNodes)
        {
            std::error_code error;
            auto const isStartup = !m_startupProject.empty()
                && std::filesystem::equivalent(project.first, m_startupProject, error);
            project.second.Content(box_value(hstring{ project.first.stem().wstring() + (isStartup ? L" (Startup)" : L"") }));
        }
    }

    void SolutionExplorerControl::SetShowAllFiles(bool value)
    {
        m_showAllFiles = value;
    }

    void SolutionExplorerControl::SetFilter(hstring const& value)
    {
        m_filter = value.c_str();
        std::transform(m_filter.begin(), m_filter.end(), m_filter.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (!m_workspaceRoot.empty())
        {
            LoadWorkspace(hstring{ m_workspaceRoot.wstring() }, hstring{ m_solutionPath.wstring() });
        }
    }

    void SolutionExplorerControl::WorkspaceTree_SelectionChanged(TreeView const& sender, TreeViewSelectionChangedEventArgs const&)
    {
        auto node = sender.SelectedNode();
        if (!node)
        {
            return;
        }

        auto const found = m_fileNodes.find(winrt::get_abi(node));
        if (found != m_fileNodes.end())
        {
            auto const path = hstring{ found->second.wstring() };
            if (m_fileOpenRequested)
            {
                m_fileOpenRequested(*this, path);
            }
            return;
        }

        auto const project = m_projectNodes.find(winrt::get_abi(node));
        if (project != m_projectNodes.end() && m_projectSelectedRequested)
        {
            m_projectSelectedRequested(*this, hstring{ project->second.first.wstring() });
        }
    }

    void SolutionExplorerControl::ContextOpen_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RaiseContextAction(L"open");
    }

    void SolutionExplorerControl::ContextProperties_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RaiseContextAction(L"properties");
    }

    void SolutionExplorerControl::ContextStartup_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RaiseContextAction(L"startup");
    }

    void SolutionExplorerControl::ContextAddItem_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RaiseContextAction(L"new-item");
    }

    void SolutionExplorerControl::ContextAddExistingItem_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RaiseContextAction(L"add-existing");
    }

    void SolutionExplorerControl::ContextRemoveFromProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RaiseContextAction(L"remove-project");
    }

    void SolutionExplorerControl::ContextRefresh_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_contextActionRequested)
        {
            m_contextActionRequested(*this, hstring{ L"refresh" });
        }
    }

    void SolutionExplorerControl::RaiseContextAction(std::wstring_view action)
    {
        if (!m_contextActionRequested)
        {
            return;
        }

        auto const node = WorkspaceTree().SelectedNode();
        std::filesystem::path path;
        if (node)
        {
            if (auto const file = m_fileNodes.find(winrt::get_abi(node)); file != m_fileNodes.end())
            {
                path = file->second;
            }
            else if (auto const project = m_projectNodes.find(winrt::get_abi(node)); project != m_projectNodes.end())
            {
                path = project->second.first;
            }
        }

        auto value = std::wstring{ action };
        if (!path.empty())
        {
            value += L"\n";
            value += path.wstring();
        }
        m_contextActionRequested(*this, hstring{ value });
    }

    void SolutionExplorerControl::AddProjectNode(TreeViewNode const& solutionNode, std::filesystem::path const& projectPath)
    {
        auto projectNode = TreeViewNode{};
        auto projectLabel = projectPath.stem().wstring();
        std::error_code startupError;
        if (!m_startupProject.empty() && std::filesystem::equivalent(projectPath, m_startupProject, startupError))
        {
            projectLabel += L" (Startup)";
        }
        projectNode.Content(box_value(hstring{ projectLabel }));
        projectNode.IsExpanded(true);
        solutionNode.Children().Append(projectNode);
        m_projectNodes.emplace(winrt::get_abi(projectNode), std::make_pair(projectPath, projectNode));

        if (m_showAllFiles)
        {
            AddFolderNodes(projectNode, projectPath.parent_path(), 0);
            return;
        }

        // Prefer the project item list so generated files and unrelated files
        // in the project directory do not appear as source files.
        std::vector<std::filesystem::path> projectFiles;
        if (projectPath.extension() == L".vcxproj")
        {
            if (std::filesystem::exists(projectPath))
            {
                try
                {
                    Windows::Data::Xml::Dom::XmlDocument document;
                    document.LoadXml(ReadXmlText(projectPath));
                    auto items = document.SelectNodes(
                        L"//*[local-name()='ClCompile' or local-name()='ClInclude' or local-name()='Page' "
                        L"or local-name()='None' or local-name()='ResourceCompile']");
                    for (uint32_t index = 0; index < items.Length(); ++index)
                    {
                        auto item = items.Item(index);
                        auto include = item.Attributes().GetNamedItem(L"Include");
                        if (!include || !include.NodeValue())
                        {
                            continue;
                        }

                        auto includePath = std::filesystem::path{ unbox_value<hstring>(include.NodeValue()).c_str() };
                        if (includePath.empty() || includePath.wstring().find(L"$(") != std::wstring::npos)
                        {
                            continue;
                        }
                        projectFiles.push_back(std::move(includePath));
                    }
                }
                catch (hresult_error const&)
                {
                    projectFiles.clear();
                }
            }
        }

        std::sort(projectFiles.begin(), projectFiles.end());
        projectFiles.erase(std::unique(projectFiles.begin(), projectFiles.end()), projectFiles.end());
        if (projectFiles.empty())
        {
            if (m_showAllFiles)
            {
                AddFolderNodes(projectNode, projectPath.parent_path(), 0);
            }
            return;
        }

        std::unordered_map<std::wstring, TreeViewNode> folders;
        for (auto const& relativePath : projectFiles)
        {
            auto normalized = relativePath.lexically_normal();
			auto searchable = normalized.wstring();
			std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t character)
			{
				return static_cast<wchar_t>(std::towlower(character));
			});
			if (!m_filter.empty() && searchable.find(m_filter) == std::wstring::npos)
			{
				continue;
			}
            auto parent = projectNode;
            std::wstring folderKey;
            auto component = normalized.begin();
            for (; component != normalized.end(); ++component)
            {
                auto const isFile = std::next(component) == normalized.end();
                if (isFile)
                {
                    auto absolute = projectPath.parent_path() / normalized;
                    if (!std::filesystem::exists(absolute) || !std::filesystem::is_regular_file(absolute))
                    {
                        break;
                    }
                    auto file = TreeViewNode{};
                    file.Content(box_value(hstring{ component->wstring() }));
                    parent.Children().Append(file);
                    m_fileNodes.emplace(winrt::get_abi(file), std::move(absolute));
                    break;
                }

                if (!folderKey.empty())
                {
                    folderKey += L"\\";
                }
                folderKey += component->wstring();
                auto found = folders.find(folderKey);
                if (found == folders.end())
                {
                    auto folder = TreeViewNode{};
                    folder.Content(box_value(hstring{ component->wstring() }));
                    folder.IsExpanded(true);
                    parent.Children().Append(folder);
                    found = folders.emplace(folderKey, folder).first;
                }
                parent = found->second;
            }
        }
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
				if (!m_filter.empty() && child.Children().Size() == 0)
				{
					auto searchable = std::filesystem::relative(entry.path(), m_workspaceRoot, error).wstring();
					std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t character)
					{
						return static_cast<wchar_t>(std::towlower(character));
					});
					if (searchable.find(m_filter) == std::wstring::npos)
					{
						parent.Children().RemoveAt(parent.Children().Size() - 1);
					}
				}
			}
			else if (entry.is_regular_file(error))
			{
				if (!m_filter.empty())
				{
					auto searchable = std::filesystem::relative(entry.path(), m_workspaceRoot, error).wstring();
					std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t character)
					{
						return static_cast<wchar_t>(std::towlower(character));
					});
					if (searchable.find(m_filter) == std::wstring::npos)
					{
						continue;
					}
				}
				auto child = TreeViewNode{};
                child.Content(box_value(hstring{ name }));
                parent.Children().Append(child);
                m_fileNodes.emplace(winrt::get_abi(child), entry.path());
            }
        }
    }
}
