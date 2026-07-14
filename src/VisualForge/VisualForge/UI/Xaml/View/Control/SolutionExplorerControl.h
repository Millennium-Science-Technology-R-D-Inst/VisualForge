#pragma once

#include "UI/Xaml/View/Control/SolutionExplorerControl.g.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <filesystem>
#include <unordered_map>

namespace winrt::VisualForge::UI::Xaml::View::Control::implementation
{
    struct SolutionExplorerControl : SolutionExplorerControlT<SolutionExplorerControl>
    {
        SolutionExplorerControl();

        void LoadWorkspace(winrt::hstring const& rootPath, winrt::hstring const& solutionPath);
        winrt::event_token FileOpenRequested(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring> const& handler);
        void FileOpenRequested(winrt::event_token const& token) noexcept;
        winrt::event_token ProjectSelectedRequested(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring> const& handler);
        void ProjectSelectedRequested(winrt::event_token const& token) noexcept;
        winrt::event_token ContextActionRequested(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring> const& handler);
        void ContextActionRequested(winrt::event_token const& token) noexcept;
        void SetStartupProject(winrt::hstring const& projectPath);
        void SetShowAllFiles(bool value);
        void SetFilter(winrt::hstring const& value);

        void WorkspaceTree_SelectionChanged(
            winrt::Microsoft::UI::Xaml::Controls::TreeView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TreeViewSelectionChangedEventArgs const& args);
        void ContextOpen_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ContextProperties_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ContextStartup_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ContextAddItem_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ContextAddExistingItem_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ContextRemoveFromProject_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void ContextRefresh_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void AddFolderNodes(
            winrt::Microsoft::UI::Xaml::Controls::TreeViewNode const& parent,
            std::filesystem::path const& folder,
            int depth);
        void RaiseContextAction(std::wstring_view action);
        void AddProjectNode(
            winrt::Microsoft::UI::Xaml::Controls::TreeViewNode const& solutionNode,
            std::filesystem::path const& projectPath);

        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring>> m_fileOpenRequested;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring>> m_projectSelectedRequested;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::hstring>> m_contextActionRequested;
        std::unordered_map<void*, std::filesystem::path> m_fileNodes;
        std::unordered_map<void*, std::pair<std::filesystem::path, winrt::Microsoft::UI::Xaml::Controls::TreeViewNode>> m_projectNodes;
        std::filesystem::path m_workspaceRoot;
        std::filesystem::path m_solutionPath;
        std::filesystem::path m_startupProject;
        std::wstring m_filter;
        bool m_showAllFiles{ false };
    };
}

namespace winrt::VisualForge::UI::Xaml::View::Control::factory_implementation
{
    struct SolutionExplorerControl : SolutionExplorerControlT<SolutionExplorerControl, implementation::SolutionExplorerControl>
    {
    };
}
