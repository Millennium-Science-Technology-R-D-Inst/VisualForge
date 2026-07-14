#include "pch.h"
#include "MainView.xaml.h"
#include "ViewModels/MainViewModel.h"
#include <winrt/Microsoft.UI.Input.h>
#if __has_include("UI/Xaml/View/Page/MainView.g.cpp")
#include "UI/Xaml/View/Page/MainView.g.cpp"
#endif

#include <iostream>
#include <algorithm>
#include <cctype>
#include "App.xaml.h"
#include "Helpers/WindowHelper.h"
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <shobjidl_core.h>

#include "Integration/MSBuildAdapter.h"
#include "Integration/GitAdapter.h"
#include "Integration/NuGetAdapter.h"
#include "Integration/ProjectWorkspaceService.h"
#include "Integration/VcpkgAdapter.h"
#include "EditorCore/Document/DocumentManager.h"
#include "Tool/ProcessSession.h"
#include "UI/Xaml/View/Control/SolutionExplorerControl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <shellapi.h>
#include <sstream>
#include <thread>

import Core.AppSettingsDatabase;

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace Helpers::WinUIWindowHelper;

namespace winrt::VisualForge::UI::Xaml::View::Page::implementation
{
    namespace
    {
        winrt::hstring ReadXmlText(std::string xml)
        {
            if (xml.size() >= 3 && static_cast<unsigned char>(xml[0]) == 0xEF
                && static_cast<unsigned char>(xml[1]) == 0xBB
                && static_cast<unsigned char>(xml[2]) == 0xBF)
            {
                xml.erase(0, 3);
            }
            return winrt::to_hstring(xml);
        }

        std::wstring ToFileUri(std::filesystem::path const& path)
        {
            return L"file:///" + path.generic_wstring();
        }

        std::wstring LanguageIdForPath(std::filesystem::path const& path)
        {
            auto extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
                           {
                               return static_cast<wchar_t>(std::towlower(value));
                           });

            if (extension == L".cpp" || extension == L".cxx" || extension == L".cc" || extension == L".h" || extension == L".hpp")
            {
                return L"cpp";
            }
            if (extension == L".xaml")
            {
                return L"xml";
            }
            return L"plaintext";
        }

        std::filesystem::path PathFromFileUri(std::wstring uri)
        {
            constexpr std::wstring_view Prefix{ L"file:///" };
            if (uri.rfind(Prefix, 0) == 0)
            {
                uri.erase(0, Prefix.size());
            }
            std::replace(uri.begin(), uri.end(), L'/', L'\\');
            return std::filesystem::path{ uri };
        }

        std::size_t Utf16LengthFromUtf8(std::string_view value)
        {
            std::size_t units{};
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                auto const first = static_cast<unsigned char>(value[index]);
                std::uint32_t codePoint{};
                std::size_t extra{};
                if ((first & 0x80) == 0)
                {
                    codePoint = first;
                }
                else if ((first & 0xE0) == 0xC0)
                {
                    codePoint = first & 0x1F;
                    extra = 1;
                }
                else if ((first & 0xF0) == 0xE0)
                {
                    codePoint = first & 0x0F;
                    extra = 2;
                }
                else if ((first & 0xF8) == 0xF0)
                {
                    codePoint = first & 0x07;
                    extra = 3;
                }
                else
                {
                    ++units;
                    continue;
                }

                if (index + extra >= value.size())
                {
                    ++units;
                    continue;
                }
                for (std::size_t count = 0; count < extra; ++count)
                {
                    auto const next = static_cast<unsigned char>(value[++index]);
                    if ((next & 0xC0) != 0x80)
                    {
                        codePoint = '?';
                        break;
                    }
                    codePoint = (codePoint << 6) | (next & 0x3F);
                }
                units += codePoint > 0xFFFF ? 2 : 1;
            }
            return units;
        }

        std::vector<std::filesystem::path> ReadSolutionProjectPaths(std::filesystem::path const& solutionPath)
        {
            std::vector<std::filesystem::path> projects;
            if (solutionPath.empty() || !std::filesystem::exists(solutionPath))
            {
                return projects;
            }

            try
            {
                std::ifstream stream{ solutionPath, std::ios::binary };
                std::string xml{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
                Windows::Data::Xml::Dom::XmlDocument document;
                document.LoadXml(ReadXmlText(std::move(xml)));
                auto nodes = document.SelectNodes(L"//*[local-name()='Project']");
                for (uint32_t index = 0; index < nodes.Length(); ++index)
                {
                    auto const pathAttribute = nodes.Item(index).Attributes().GetNamedItem(L"Path");
                    if (!pathAttribute || !pathAttribute.NodeValue())
                    {
                        continue;
                    }
                    auto const relative = std::filesystem::path{
                        std::wstring{ unbox_value<hstring>(pathAttribute.NodeValue()) } };
                    auto const project = (solutionPath.parent_path() / relative).lexically_normal();
                    if (project.extension() == L".vcxproj" && std::filesystem::exists(project))
                    {
                        projects.push_back(project);
                    }
                }
            }
            catch (...)
            {
                projects.clear();
            }
            return projects;
        }
    }

    MainView::MainView()
    {
        m_viewModel = make<winrt::VisualForge::ViewModels::implementation::MainViewModel>();
    }

    MainView::~MainView()
    {
        if (m_isNavigationCallbackRegistered)
        {
            m_canGoBackChanged.remove(m_canGoBackChangedToken);
        }
        // XAML named elements may already be disconnected during teardown.
        // Persist UI state only after the page has completed its Loaded phase.
        if (m_isPageInitialized)
        {
            SaveEditorSession();
            SaveBreakpoints();
        }
        if (m_languageServiceTimer)
        {
            m_languageServiceTimer.Stop();
        }
        m_languageService.Stop();
        m_debugAdapter.Stop();
        m_terminalSession.Stop();
    }

    void MainView::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (!m_isPageInitialized)
        {
            m_isPageInitialized = true;
            auto const theme = Core::AppSettingsDatabase::Instance().GetString(
                Core::AppSettingsDatabase::CAT_UI, "ui.theme").value_or("default");
            this->RequestedTheme(theme == "dark" ? ElementTheme::Dark : theme == "light" ? ElementTheme::Light : ElementTheme::Default);
            DataContext(m_viewModel);
            ConfigureDocumentTab(ActiveDocumentTab());
            EditorTabs().SelectedItem(ActiveDocumentTab());
            LoadRecentWorkspaces();
            m_solutionFileOpenToken = SolutionExplorer().FileOpenRequested([this](IInspectable const&, hstring const& path)
                                                                           {
                                                                               OpenPathAsync(std::filesystem::path{ path.c_str() });
                                                                           });
            m_solutionProjectSelectedToken = SolutionExplorer().ProjectSelectedRequested([this](IInspectable const&, hstring const& path)
                                                                                         {
                                                                                             SolutionProjectSelected(*this, path);
                                                                                         });
            m_solutionContextActionToken = SolutionExplorer().ContextActionRequested([this](IInspectable const&, hstring const& request)
                                                                                     {
                                                                                         SolutionContextActionRequested(*this, request);
                                                                                     });
            m_canGoBackChangedToken = CanGoBackChanged([this](IInspectable const&, bool canGoBack)
                                                       {
                                                           AppTitleBar().IsBackButtonVisible(canGoBack);
                                                       });
            m_isNavigationCallbackRegistered = true;
        }

        RefreshStatusBar();
        LoadWorkspaceLayouts();
        StartTerminalSession();

        auto window = winrt::VisualForge::implementation::App::window;
        window.ExtendsContentIntoTitleBar(true);
        if (auto appWindow = window.AppWindow())
        {
            appWindow.TitleBar().PreferredHeightOption(winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Standard);
            window.SetTitleBar(AppTitleBar());
            PlacementRestoration::Enable(window);
#ifdef _DEBUG
            {
                AppTitleBar().Subtitle(L"Dev");
            }
#endif
        }
    }

    void MainView::InvertAppThemeButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        this->RequestedTheme(this->RequestedTheme() == ElementTheme::Dark ? ElementTheme::Light : ElementTheme::Dark);
    }

    void MainView::OpenFile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        OpenFileAsync();
    }

    void MainView::NewFile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NewFileAsync();
    }

    void MainView::NewProjectItem_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NewProjectItemAsync();
    }

    void MainView::NewProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NewProjectAsync();
    }

    void MainView::OpenWorkspace_Click(IInspectable const&, RoutedEventArgs const&)
    {
        OpenWorkspaceAsync();
    }

    void MainView::RecentWorkspace_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto item = sender.try_as<MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto const path = std::filesystem::path{
            unbox_value_or<hstring>(item.Tag(), L"").c_str() };
        if (!path.empty())
        {
            OpenRecentWorkspaceAsync(path);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenRecentWorkspaceAsync(
        std::filesystem::path const& solutionPath)
    {
        try
        {
            if (!std::filesystem::exists(solutionPath))
            {
                SetOutput(L"Workspace", L"Recent workspace no longer exists: " + solutionPath.wstring());
                LoadRecentWorkspaces();
                co_return;
            }

            co_await OpenWorkspaceFileAsync(co_await StorageFile::GetFileFromPathAsync(solutionPath.wstring()));
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Workspace", L"Recent workspace could not be opened: " + std::wstring{ error.message() });
        }
    }

    void MainView::LoadRecentWorkspaces()
    {
        m_recentWorkspaces.clear();
        auto const value = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI, "recent.workspaces").value_or(L"");
        std::size_t start{};
        while (start <= value.size())
        {
            auto const separator = value.find(L'|', start);
            auto const text = value.substr(start,
                                           separator == std::wstring::npos ? std::wstring::npos : separator - start);
            if (!text.empty())
            {
                auto path = std::filesystem::path{ text };
                if (std::filesystem::exists(path)
                    && (path.extension() == L".slnx" || path.extension() == L".sln"))
                {
                    m_recentWorkspaces.push_back(std::move(path));
                }
            }
            if (separator == std::wstring::npos)
            {
                break;
            }
            start = separator + 1;
        }
        RefreshRecentWorkspacesMenu();
    }

    void MainView::RecordRecentWorkspace(std::filesystem::path const& solutionPath)
    {
        if (solutionPath.empty())
        {
            return;
        }

        auto const normalized = solutionPath.lexically_normal();
        m_recentWorkspaces.erase(std::remove_if(m_recentWorkspaces.begin(), m_recentWorkspaces.end(),
                                                [&](std::filesystem::path const& path)
                                                {
                                                    return path.lexically_normal() == normalized;
                                                }), m_recentWorkspaces.end());
        m_recentWorkspaces.insert(m_recentWorkspaces.begin(), normalized);
        if (m_recentWorkspaces.size() > 10)
        {
            m_recentWorkspaces.resize(10);
        }

        std::wstring serialized;
        for (auto const& path : m_recentWorkspaces)
        {
            if (!serialized.empty())
            {
                serialized += L"|";
            }
            serialized += path.wstring();
        }
        Core::AppSettingsDatabase::Instance().SetStringW(
            Core::AppSettingsDatabase::CAT_UI, "recent.workspaces", serialized);
        RefreshRecentWorkspacesMenu();
    }

    void MainView::SaveEditorSession()
    {
        if (!m_isPageInitialized || m_solutionPath.empty())
        {
            return;
        }

        std::wstring serialized;
        if (auto const active = EditorTabs().SelectedItem().try_as<TabViewItem>())
        {
            if (auto const found = m_documentTabs.find(winrt::get_abi(active)); found != m_documentTabs.end()
                && !found->second.Path.empty())
            {
                serialized += L"active\t" + found->second.Path.lexically_normal().wstring() + L"\n";
            }
        }

        for (uint32_t index = 0; index < EditorTabs().TabItems().Size(); ++index)
        {
            auto const tab = EditorTabs().TabItems().GetAt(index).try_as<TabViewItem>();
            if (!tab)
            {
                continue;
            }
            auto const found = m_documentTabs.find(winrt::get_abi(tab));
            if (found == m_documentTabs.end())
            {
                continue;
            }
            auto const& state = found->second;
            if (state.Path.empty())
            {
                continue;
            }
            serialized += L"tab\t" + state.Path.lexically_normal().wstring()
                + L"\t" + std::to_wstring((std::max)(0, state.Editor.SelectionStart()))
                + L"\t" + std::to_wstring((std::max)(0, state.Editor.SelectionLength()))
                + L"\t" + std::to_wstring((std::max)(0, state.Editor.ScrollLine()))
                + L"\t" + std::to_wstring((std::max)(0, state.Editor.ScrollColumn())) + L"\n";
        }

        Core::AppSettingsDatabase::Instance().SetStringW(
            Core::AppSettingsDatabase::CAT_UI,
            "workspace.session." + winrt::to_string(hstring{ m_solutionPath.wstring() }),
            serialized);
    }

    void MainView::LoadBreakpoints()
    {
        m_breakpoints.clear();
        if (m_solutionPath.empty())
        {
            return;
        }

        auto const serialized = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI,
            "workspace.breakpoints." + winrt::to_string(hstring{ m_solutionPath.wstring() })).value_or(L"");
        std::wistringstream records{ serialized };
        std::wstring record;
        while (std::getline(records, record))
        {
            auto const separator = record.find(L'\t');
            if (separator == std::wstring::npos || separator == 0)
            {
                continue;
            }

            auto path = std::filesystem::path{ record.substr(0, separator) }.lexically_normal();
            auto& lines = m_breakpoints[path.wstring()];
            std::wistringstream lineValues{ record.substr(separator + 1) };
            std::wstring value;
            while (std::getline(lineValues, value, L','))
            {
                try
                {
                    auto const line = std::stoi(value);
                    if (line > 0)
                    {
                        lines.push_back(line);
                    }
                }
                catch (...)
                {
                }
            }
            std::sort(lines.begin(), lines.end());
            lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
            if (lines.empty())
            {
                m_breakpoints.erase(path.wstring());
            }
        }
    }

    void MainView::SaveBreakpoints() const
    {
        if (m_solutionPath.empty())
        {
            return;
        }

        std::wstring serialized;
        for (auto const& [path, lines] : m_breakpoints)
        {
            if (lines.empty())
            {
                continue;
            }
            if (!serialized.empty())
            {
                serialized += L'\n';
            }
            serialized += path + L'\t';
            for (std::size_t index = 0; index < lines.size(); ++index)
            {
                if (index != 0)
                {
                    serialized += L',';
                }
                serialized += std::to_wstring(lines[index]);
            }
        }

        Core::AppSettingsDatabase::Instance().SetStringW(
            Core::AppSettingsDatabase::CAT_UI,
            "workspace.breakpoints." + winrt::to_string(hstring{ m_solutionPath.wstring() }),
            serialized);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RestoreEditorSessionAsync()
    {
        if (m_solutionPath.empty())
        {
            co_return;
        }

        auto const serialized = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI,
            "workspace.session." + winrt::to_string(hstring{ m_solutionPath.wstring() })).value_or(L"");
        if (serialized.empty())
        {
            co_return;
        }

        struct SessionEntry
        {
            std::filesystem::path Path;
            int32_t SelectionStart{};
            int32_t SelectionLength{};
            int32_t ScrollLine{};
            int32_t ScrollColumn{};
        };

        std::vector<SessionEntry> entries;
        std::unordered_set<std::wstring> seenPaths;
        std::filesystem::path activePath;
        std::wistringstream lines{ serialized };
        std::wstring line;
        while (std::getline(lines, line))
        {
            std::vector<std::wstring> fields;
            std::wistringstream fieldsStream{ line };
            std::wstring field;
            while (std::getline(fieldsStream, field, L'\t'))
            {
                fields.push_back(std::move(field));
            }
            if (fields.empty())
            {
                continue;
            }
            try
            {
                if (fields[0] == L"active" && fields.size() >= 2)
                {
                    activePath = std::filesystem::path{ fields[1] }.lexically_normal();
                }
                else if (fields[0] == L"tab" && fields.size() >= 6)
                {
                    auto const normalizedPath = std::filesystem::path{ fields[1] }.lexically_normal();
                    if (!seenPaths.insert(normalizedPath.wstring()).second)
                    {
                        continue;
                    }
                    entries.push_back({
                        normalizedPath,
                        static_cast<int32_t>(std::stol(fields[2])),
                        static_cast<int32_t>(std::stol(fields[3])),
                        static_cast<int32_t>(std::stol(fields[4])),
                        static_cast<int32_t>(std::stol(fields[5])) });
                }
            }
            catch (...)
            {
                continue;
            }
        }

        for (auto const& entry : entries)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(entry.Path, error))
            {
                continue;
            }
            co_await OpenPathAsync(entry.Path);
            for (auto const& [_, state] : m_documentTabs)
            {
                if (state.Path.lexically_normal() == entry.Path)
                {
                    state.Editor.Select(entry.SelectionStart, entry.SelectionLength);
                    state.Editor.SetScrollPosition(entry.ScrollLine, entry.ScrollColumn);
                    break;
                }
            }
        }

        if (!activePath.empty())
        {
            for (auto const& [_, state] : m_documentTabs)
            {
                if (state.Path.lexically_normal() == activePath)
                {
                    EditorTabs().SelectedItem(state.Tab);
                    state.Editor.FocusEditor();
                    break;
                }
            }
        }
        UpdateEditorPosition();
    }

    void MainView::RefreshRecentWorkspacesMenu()
    {
        if (!RecentWorkspacesMenu())
        {
            return;
        }

        RecentWorkspacesMenu().Items().Clear();
        if (m_recentWorkspaces.empty())
        {
            auto empty = MenuFlyoutItem{};
            empty.Text(L"No recent workspaces");
            empty.IsEnabled(false);
            RecentWorkspacesMenu().Items().Append(empty);
            return;
        }

        for (auto const& path : m_recentWorkspaces)
        {
            auto item = MenuFlyoutItem{};
            item.Text(hstring{ path.stem().wstring() + L"  -  " + path.parent_path().wstring() });
            item.Tag(box_value(hstring{ path.wstring() }));
            item.Click([weakThis = get_weak()](IInspectable const& sender, RoutedEventArgs const& args)
                       {
                           if (auto strongThis = weakThis.get())
                           {
                               strongThis->RecentWorkspace_Click(sender, args);
                           }
                       });
            RecentWorkspacesMenu().Items().Append(item);
        }
    }

    void MainView::SolutionProjectSelected(IInspectable const&, hstring const& projectPath)
    {
        if (projectPath.empty())
        {
            return;
        }

        m_selectedProjectPath = std::filesystem::path{ projectPath.c_str() };
        m_isRightToolWindowHidden = false;
        RightToolWindowGrid().Visibility(Visibility::Visible);
        RightToolTabs().SelectedIndex(2);
        UpdatePropertiesPanel();
    }

    void MainView::SolutionContextActionRequested(IInspectable const&, hstring const& request)
    {
        auto const value = std::wstring{ request };
        auto const separator = value.find(L'\n');
        auto const action = value.substr(0, separator);
        auto const path = separator == std::wstring::npos ? std::wstring{} : value.substr(separator + 1);

        if (action == L"refresh")
        {
            RefreshSolutionExplorer_Click(*this, RoutedEventArgs{});
            return;
        }
        if (path.empty())
        {
            SetOutput(L"Solution Explorer", L"Select a file or project before using the context command.");
            return;
        }

        auto const selectedPath = std::filesystem::path{ path };
        if (action == L"open")
        {
            OpenPathAsync(selectedPath);
        }
        else if (action == L"new-item" && selectedPath.extension() == L".vcxproj")
        {
            m_selectedProjectPath = selectedPath;
            NewProjectItemAsync();
        }
        else if (action == L"add-existing" && selectedPath.extension() == L".vcxproj")
        {
            m_selectedProjectPath = selectedPath;
            AddExistingProjectItemAsync(selectedPath);
        }
        else if (action == L"remove-project")
        {
            auto projectPath = FindOwningProject(selectedPath);
            if (projectPath.empty())
            {
                SetOutput(L"Project", L"The selected file is not registered in a C++ project.");
            }
            else
            {
                std::wstring error;
                if (::VisualForge::Integration::ProjectWorkspaceService::RemoveItem(projectPath, selectedPath, error))
                {
                    PopulateWorkspaceTree();
                    SetOutput(L"Project", L"Removed the file reference from " + projectPath.filename().wstring());
                }
                else
                {
                    SetOutput(L"Project", error);
                }
            }
        }
        else if (action == L"properties" && selectedPath.extension() == L".vcxproj")
        {
            m_selectedProjectPath = selectedPath;
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            RightToolTabs().SelectedIndex(2);
            UpdatePropertiesPanel();
        }
        else if (action == L"startup" && selectedPath.extension() == L".vcxproj")
        {
            m_selectedProjectPath = selectedPath;
            SetStartupProject_Click(*this, RoutedEventArgs{});
        }
        else
        {
            SetOutput(L"Solution Explorer", L"That context command is not available for the selected node.");
        }
    }

    void MainView::SaveFile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SaveFileAsync();
    }

    void MainView::SaveAs_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (selected)
        {
            SaveDocumentAsAsync(selected);
        }
    }

    void MainView::RenameActiveFile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RenameActiveFileAsync();
    }

    void MainView::DeleteActiveFile_Click(IInspectable const&, RoutedEventArgs const&)
    {
        DeleteActiveFileAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainView::NewFileAsync()
    {
        auto& document = m_documentManager.NewUntitled();
        auto const documentKey = document.Path();
        auto const key = documentKey.wstring();
        m_isLoadingDocument = true;
        auto tab = TabViewItem{};
        auto editor = winrt::VisualForge::UI::Xaml::View::Control::EditorDocumentControl{ nullptr };
        if (!m_firstDocumentTabInUse)
        {
            tab = ActiveDocumentTab();
            editor = MainEditorControl();
            m_firstDocumentTabInUse = true;
        }
        else
        {
            tab.IsClosable(true);
            tab.Content(editor);
            EditorTabs().TabItems().Append(tab);
        }
        ConfigureDocumentTab(tab);

        auto const tabKey = winrt::get_abi(tab);
        m_documentTabs.emplace(tabKey, DocumentTabState{ nullptr, tab, editor, {}, documentKey, {} });
        auto const weakThis = get_weak();
        editor.TextChanged([weakThis, tabKey](IInspectable const&, IInspectable const&)
                           {
                               if (auto strongThis = weakThis.get(); strongThis && !strongThis->m_isLoadingDocument)
                               {
                                   strongThis->SynchronizeDocument(tabKey);
                                   strongThis->RequestAutomaticCompletion(tabKey);
                               }
                           });
        editor.SelectionChanged([weakThis](IInspectable const&, IInspectable const&)
                                {
                                    if (auto strongThis = weakThis.get())
                                    {
                                        strongThis->UpdateEditorPosition();
                                    }
                                });
        editor.CompletionRequested([weakThis, tabKey](IInspectable const&, IInspectable const&)
                                   {
                                       if (auto strongThis = weakThis.get())
                                       {
                                           strongThis->RequestCompletionForDocument(tabKey);
                                       }
                                   });
        editor.BreakpointRequested([weakThis, tabKey](IInspectable const&, int32_t line)
                                   {
                                       if (auto strongThis = weakThis.get())
                                       {
                                           strongThis->ToggleBreakpointForDocument(tabKey, line);
                                       }
                                   });
        editor.SetText(L"");
        m_isLoadingDocument = false;
        UpdateDocumentHeader(tabKey);
        EditorTabs().SelectedItem(tab);
        UpdateEditorPosition();
        SetOutput(L"Editor", L"Created " + key);
        ShellStatusText().Text(L"New file");
        co_return;
    }

    winrt::Windows::Foundation::IAsyncAction MainView::NewProjectItemAsync()
    {
        try
        {
            if (m_workspaceRoot.empty())
            {
                SetOutput(L"Project", L"Open a workspace before adding a project item.");
                co_return;
            }

            auto projectPath = (m_selectedProjectPath.extension() == L".vcxproj"
                                && std::filesystem::exists(m_selectedProjectPath))
                ? m_selectedProjectPath
                : (m_startupProjectPath.empty() ? FindWorkspaceFile(L".vcxproj") : m_startupProjectPath);
            if (projectPath.empty() || !std::filesystem::exists(projectPath))
            {
                SetOutput(L"Project", L"Select or open a C++ project before adding an item.");
                co_return;
            }

            StackPanel content;
            content.Spacing(8);
            TextBlock nameLabel;
            nameLabel.Text(L"File name");
            content.Children().Append(nameLabel);
            TextBox nameBox;
            nameBox.PlaceholderText(L"NewClass");
            content.Children().Append(nameBox);
            TextBlock templateLabel;
            templateLabel.Text(L"Item type");
            content.Children().Append(templateLabel);
            ComboBox templateBox;
            templateBox.Items().Append(box_value(L"C++ source (.cpp)"));
            templateBox.Items().Append(box_value(L"C++ header (.h)"));
            templateBox.Items().Append(box_value(L"XAML page (.xaml)"));
            templateBox.Items().Append(box_value(L"Text file (.txt)"));
            templateBox.SelectedIndex(0);
            content.Children().Append(templateBox);

            ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(box_value(L"Add New Item"));
            dialog.Content(content);
            dialog.PrimaryButtonText(L"Add");
            dialog.CloseButtonText(L"Cancel");
            if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
            {
                co_return;
            }

            auto baseName = std::wstring{ nameBox.Text() };
            if (baseName.empty())
            {
                SetOutput(L"Project", L"The item name cannot be empty.");
                co_return;
            }
            for (auto const character : baseName)
            {
                if (!(std::iswalnum(character) || character == L'_' || character == L'-' || character == L'.'))
                {
                    SetOutput(L"Project", L"Use a single file name without directory separators.");
                    co_return;
                }
            }

            static std::array<std::wstring, 4> const extensions{ L".cpp", L".h", L".xaml", L".txt" };
            static std::array<std::wstring, 4> const itemKinds{ L"ClCompile", L"ClInclude", L"Page", L"None" };
            static std::array<std::string, 4> const templates{
                "#include <iostream>\n\nint main()\n{\n    return 0;\n}\n",
                "#pragma once\n",
                "<Page\n    xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"\n    xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\" />\n",
                "" };
            auto const templateIndex = static_cast<std::size_t>((std::max)(0, templateBox.SelectedIndex()));
            if (baseName.length() < extensions[templateIndex].length()
                || baseName.substr(baseName.length() - extensions[templateIndex].length()) != extensions[templateIndex])
            {
                baseName += extensions[templateIndex];
            }
            const auto newFilePath = projectPath.parent_path() / baseName;
            if (std::filesystem::exists(newFilePath))
            {
                SetOutput(L"Project", L"The project item already exists.");
                co_return;
            }

            std::ofstream sourceStream(newFilePath, std::ios::binary);
            sourceStream << templates[templateIndex];
            if (!sourceStream)
            {
                SetOutput(L"Project", L"Could not create the new project item.");
                co_return;
            }
            sourceStream.close();

            std::wstring projectError;
            if (!::VisualForge::Integration::ProjectWorkspaceService::AddItem(
                projectPath, newFilePath, itemKinds[templateIndex], projectError))
            {
                std::filesystem::remove(newFilePath);
                SetOutput(L"Project", projectError);
                co_return;
            }

            PopulateWorkspaceTree();
            SetOutput(L"Project", L"Added " + newFilePath.wstring());
            co_await OpenStorageFileAsync(co_await StorageFile::GetFileFromPathAsync(newFilePath.wstring()));
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Project", L"Add item failed: " + std::wstring{ error.message() });
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Project", std::wstring{ L"Add item failed: " } + winrt::to_hstring(error.what()).c_str());
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::AddExistingProjectItemAsync(std::filesystem::path const& projectPath)
    {
        if (projectPath.empty() || !std::filesystem::exists(projectPath))
        {
            SetOutput(L"Project", L"Select a valid C++ project first.");
            co_return;
        }

        try
        {
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::List);
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");
            auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(
                WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
            auto file = co_await picker.PickSingleFileAsync();
            if (!file)
            {
                co_return;
            }

            auto const filePath = std::filesystem::path{ file.Path().c_str() };
            std::error_code relativeError;
            auto const relative = std::filesystem::relative(filePath, m_workspaceRoot, relativeError);
            if (relativeError || relative.empty() || relative.native().rfind(L"..", 0) == 0)
            {
                SetOutput(L"Project", L"Only files inside the current workspace can be added.");
                co_return;
            }

            std::wstring itemKind{ L"None" };
            auto extension = filePath.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
                           {
                               return static_cast<wchar_t>(std::towlower(value));
                           });
            if (extension == L".cpp" || extension == L".c" || extension == L".cc" || extension == L".cxx")
            {
                itemKind = L"ClCompile";
            }
            else if (extension == L".h" || extension == L".hh" || extension == L".hpp" || extension == L".hxx")
            {
                itemKind = L"ClInclude";
            }
            else if (extension == L".xaml")
            {
                itemKind = L"Page";
            }

            std::wstring error;
            if (!::VisualForge::Integration::ProjectWorkspaceService::AddItem(
                projectPath, filePath, std::move(itemKind), error))
            {
                SetOutput(L"Project", error);
                co_return;
            }

            PopulateWorkspaceTree();
            SetOutput(L"Project", L"Added existing item " + filePath.filename().wstring());
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Project", L"Add existing item failed: " + std::wstring{ error.message() });
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Project", L"Add existing item failed: " + std::wstring{ winrt::to_hstring(error.what()) });
        }
    }

    void MainView::SaveAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SaveAllDocumentsAsync();
    }

    void MainView::ExitMenu_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ExitAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ExitAsync()
    {
        try
        {
            if (!co_await CloseDocumentsForWorkspaceSwitchAsync())
            {
                co_return;
            }

            if (auto window = winrt::VisualForge::implementation::App::window)
            {
                window.Close();
            }
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Application", L"Exit failed: " + std::wstring{ error.message() });
        }
    }

    void MainView::BuildProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        BuildProjectAsync();
    }

    void MainView::BuildProjectOnly_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RunBuildAsync(L"Build", L"Build startup project", true);
    }

    void MainView::RebuildProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RunBuildAsync(L"Rebuild", L"Rebuild solution", false);
    }

    void MainView::CleanProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RunBuildAsync(L"Clean", L"Clean solution", false);
    }

    void MainView::RunProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
        auto const solution = FindWorkspaceFile(L".slnx");
        auto const configurationItem = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>();
        auto const platformItem = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>();
        auto const configuration = configurationItem ? std::wstring{ unbox_value<hstring>(configurationItem.Content()) } : L"Debug";
        auto const platform = platformItem ? std::wstring{ unbox_value<hstring>(platformItem.Content()) } : L"x64";
        auto executable = FindBuiltExecutable(configuration, platform);
        if (solution.empty())
        {
            SetOutput(L"Debug", L"Open a workspace solution before starting a local executable.");
            return;
        }
        if (!std::filesystem::exists(executable))
        {
            m_launchAfterBuild = true;
            m_launchWithoutDebuggingAfterBuild = false;
            SetOutput(L"Debug", L"Executable not found; building before starting the debugger.");
            BuildProjectAsync();
            return;
        }

        auto const dapPath = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI, "tools.dap.path").value_or(L"lldb-dap.exe");
        if (m_debugAdapter.Start(dapPath, solution.parent_path()))
        {
            m_debugThreadId = 1;
            m_selectedFrameId = 0;
            LocalsTree().RootNodes().Clear();
            m_pendingVariableRequests.clear();
            m_debugAdapter.Launch(executable.wstring(), L"[]", solution.parent_path().wstring(), true);
            m_debugAdapter.ConfigurationDone();
            for (auto const& [path, lines] : m_breakpoints)
            {
                m_debugAdapter.SetBreakpoints(path, lines);
            }
            if (m_viewModel)
            {
                winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkDebugStarting();
            }
            DebugSessionText().Text(L"LLDB-DAP session started; waiting for debugger events.");
            DebugEventsList().Items().Clear();
            auto event = ListViewItem{};
            event.Content(box_value(hstring{ L"Launch request sent: VisualForge.exe (" + configuration + L"/" + platform + L")" }));
            DebugEventsList().Items().Append(event);
            SetOutput(L"Debug", L"LLDB-DAP launched " + executable.wstring());
            ShellStatusText().Text(L"Running");
        }
        else
        {
            // Keep the prototype useful on machines without lldb-dap.
            auto const result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, solution.parent_path().c_str(), SW_SHOWNORMAL));
            if (result > 32)
            {
                SetOutput(L"Debug", L"lldb-dap unavailable; started target without debugger.");
                DebugSessionText().Text(L"Target started without debugger adapter.");
            }
            else
            {
                SetOutput(L"Debug", L"Unable to start " + executable.wstring());
            }
        }
    }

    void MainView::RunWithoutDebugging_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Editing, true);
        auto const solution = FindWorkspaceFile(L".slnx");
        auto const configurationItem = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>();
        auto const platformItem = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>();
        auto const configuration = configurationItem ? std::wstring{ unbox_value<hstring>(configurationItem.Content()) } : L"Debug";
        auto const platform = platformItem ? std::wstring{ unbox_value<hstring>(platformItem.Content()) } : L"x64";
        auto executable = FindBuiltExecutable(configuration, platform);
        if (solution.empty())
        {
            SetOutput(L"Run", L"Open a workspace solution before starting without debugging.");
            return;
        }
        if (!std::filesystem::exists(executable))
        {
            m_launchAfterBuild = true;
            m_launchWithoutDebuggingAfterBuild = true;
            SetOutput(L"Run", L"Executable not found; building before starting.");
            BuildProjectAsync();
            return;
        }

        auto const result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
            nullptr,
            L"open",
            executable.c_str(),
            nullptr,
            solution.parent_path().c_str(),
            SW_SHOWNORMAL));
        if (result > 32)
        {
            SetOutput(L"Run", L"Started without debugging: " + executable.wstring());
            ShellStatusText().Text(L"Running");
        }
        else
        {
            SetOutput(L"Run", L"Unable to start " + executable.wstring());
        }
    }

    void MainView::ContinueDebug_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            SetOutput(L"DAP", L"No active debug session.");
            return;
        }
        m_debugAdapter.Continue(m_debugThreadId);
        SetOutput(L"DAP", L"Continue requested.");
    }

    void MainView::StepOverDebug_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            SetOutput(L"DAP", L"No active debug session.");
            return;
        }
        m_debugAdapter.Next(m_debugThreadId);
        SetOutput(L"DAP", L"Step over requested.");
    }

    void MainView::StepInDebug_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            SetOutput(L"DAP", L"No active debug session.");
            return;
        }
        m_debugAdapter.StepIn(m_debugThreadId);
        SetOutput(L"DAP", L"Step in requested.");
    }

    void MainView::StepOutDebug_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            SetOutput(L"DAP", L"No active debug session.");
            return;
        }
        m_debugAdapter.StepOut(m_debugThreadId);
        SetOutput(L"DAP", L"Step out requested.");
    }

    void MainView::StopDebug_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_debugAdapter.Stop();
        ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Editing, true);
        m_selectedFrameId = 0;
        m_debugThreadId = 1;
        LocalsTree().RootNodes().Clear();
        m_pendingVariableRequests.clear();
        for (auto& [_, state] : m_documentTabs)
        {
            state.Editor.SetDebugLine(0);
        }
        if (m_viewModel)
        {
            winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkDebugStopped();
        }
        DebugSessionText().Text(L"Debug session stopped.");
        SetOutput(L"Debug", L"Debug session stopped.");
        RefreshStatusBar();
    }

    void MainView::CancelBuild_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_buildRunning.load())
        {
            SetOutput(L"Build", L"No build is running.");
            return;
        }

        m_buildCancelRequested = true;
        SetOutput(L"Build", L"Build cancellation requested.");
    }

    void MainView::ClearOutput_Click(IInspectable const&, RoutedEventArgs const&)
    {
        OutputTextBox().Text(L"");
    }

    void MainView::StartTerminalSession()
    {
        if (m_terminalSession.IsRunning())
        {
            return;
        }

        ::VisualForge::Tool::ToolCommand command;
        command.Kind = ::VisualForge::Tool::ToolKind::Compiler;
        command.Executable = L"powershell.exe";
        command.Arguments = { L"-NoLogo", L"-NoProfile", L"-NoExit" };
        command.WorkingDirectory = m_workspaceRoot.empty() ? std::filesystem::current_path() : m_workspaceRoot;
        command.DisplayName = L"VisualForge Developer PowerShell";
        m_terminalOutputBytes = 0;
        if (!m_terminalSession.Start(command))
        {
            TerminalOutputTextBox().Text(L"Unable to start powershell.exe.");
            return;
        }

        TerminalOutputTextBox().Text(L"VisualForge Developer PowerShell\r\n");
        if (!m_languageServiceTimer)
        {
            auto weakThis = get_weak();
            m_languageServiceTimer = DispatcherQueue().CreateTimer();
            m_languageServiceTimer.Interval(Windows::Foundation::TimeSpan{ 1000000 });
            m_languageServiceTimer.Tick([weakThis](IInspectable const&, IInspectable const&)
                                        {
                                            if (auto strongThis = weakThis.get())
                                            {
                                                strongThis->PollLanguageService();
                                                strongThis->PollDebugAdapter();
                                                strongThis->PollTerminal();
                                                strongThis->PollExternalChanges();
                                            }
                                        });
            m_languageServiceTimer.Start();
        }
    }

    void MainView::PollTerminal()
    {
        if (!m_terminalSession.IsRunning() && m_terminalSession.State() != ::VisualForge::Tool::ProcessSessionState::Exited)
        {
            return;
        }

        auto snapshot = m_terminalSession.Snapshot();
        auto combined = snapshot.StdOut + snapshot.StdErr;
        if (combined.size() <= m_terminalOutputBytes)
        {
            return;
        }

        auto const unread = combined.substr(m_terminalOutputBytes);
        m_terminalOutputBytes = combined.size();
        auto text = std::wstring{ unread.begin(), unread.end() };
        auto current = std::wstring{ TerminalOutputTextBox().Text() };
        current += text;
        if (current.size() > 30000)
        {
            current.erase(0, current.size() - 30000);
        }
        TerminalOutputTextBox().Text(hstring{ current });
    }

    void MainView::PollExternalChanges()
    {
        for (auto& [key, state] : m_documentTabs)
        {
            if (state.Path.empty() || !std::filesystem::exists(state.Path))
            {
                continue;
            }

            std::error_code timestampError;
            auto const timestamp = std::filesystem::last_write_time(state.Path, timestampError);
            if (timestampError || timestamp == state.LastWriteTime)
            {
                continue;
            }

            if (state.IsDirty)
            {
                if (!state.ExternalChangePending)
                {
                    state.ExternalChangePending = true;
                    SetOutput(L"Editor", L"File changed on disk while it has unsaved edits: " + state.Path.wstring());
                }
                continue;
            }

            state.LastWriteTime = timestamp;
            state.ExternalChangePending = false;
            ReloadExternalDocumentAsync(key);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ReloadExternalDocumentAsync(void* key)
    {
        auto const weakThis = get_weak();
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            co_return;
        }

        auto const path = found->second.Path;
        auto const documentKey = found->second.DocumentKey;
        try
        {
            auto const text = ::VisualForge::EditorCore::Document::DocumentManager::ReadTextFile(path);
            auto strongThis = weakThis.get();
            if (!strongThis)
            {
                co_return;
            }
            auto document = strongThis->m_documentManager.Find(documentKey);
            if (!document)
            {
                co_return;
            }

            document->LoadText(text);
            strongThis->ReloadEditorFromDocument(key);
            if (auto const current = strongThis->m_documentTabs.find(key); current != strongThis->m_documentTabs.end())
            {
                current->second.LastText = text;
                current->second.IsDirty = false;
                current->second.ExternalChangePending = false;
                if (strongThis->m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
                {
                    strongThis->m_languageService.DidChange(ToFileUri(path), text);
                }
            }
            strongThis->SetOutput(L"Editor", L"Reloaded external changes: " + path.wstring());
        }
        catch (winrt::hresult_error const& error)
        {
            if (auto strongThis = weakThis.get())
            {
                strongThis->SetOutput(L"Editor", L"External reload failed: " + std::wstring{ error.message() });
            }
        }
        catch (std::exception const& error)
        {
            if (auto strongThis = weakThis.get())
            {
                strongThis->SetOutput(L"Editor", std::wstring{ L"External reload failed: " } + winrt::to_hstring(error.what()).c_str());
            }
        }
    }

    void MainView::ExecuteTerminal_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto const command = std::wstring{ TerminalCommandBox().Text() };
        if (command.empty())
        {
            return;
        }
        StartTerminalSession();
        if (!m_terminalSession.IsRunning())
        {
            return;
        }

        auto const line = winrt::to_string(hstring{ command }) + "\r\n";
        if (m_terminalSession.Write(line))
        {
            auto current = std::wstring{ TerminalOutputTextBox().Text() };
            current += L"> " + command + L"\r\n";
            TerminalOutputTextBox().Text(hstring{ current });
            TerminalCommandBox().Text(L"");
        }
    }

    void MainView::TerminalCommandBox_KeyDown(
        IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Enter)
        {
            ExecuteTerminal_Click(sender, args);
            args.Handled(true);
        }
    }

    void MainView::ViewToolWindow_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto item = sender.try_as<MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto const title = std::wstring{ item.Text() };
        if (title == L"Solution Explorer")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            if (m_viewModel)
            {
                (void)winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(L"solutionExplorer", ::VisualForge::IDE::Shell::DockCommandKind::Activate);
            }
            RightToolTabs().SelectedIndex(0);
        }
        else if (title == L"Git Changes" || title == L"Git Repository")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            if (m_viewModel)
            {
                (void)winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(L"gitChanges", ::VisualForge::IDE::Shell::DockCommandKind::Activate);
            }
            RightToolTabs().SelectedIndex(1);
        }
        else if (title == L"Properties Window")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            if (m_viewModel)
            {
                (void)winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(L"properties", ::VisualForge::IDE::Shell::DockCommandKind::Activate);
            }
            RightToolTabs().SelectedIndex(2);
        }
        else if (title == L"Document Outline")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            RightToolTabs().SelectedIndex(4);
            RequestDocumentSymbolsForActiveDocument();
        }
        else if (title == L"Debug Windows")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            if (m_viewModel)
            {
                (void)winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(L"diagnostics", ::VisualForge::IDE::Shell::DockCommandKind::Activate);
            }
            ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
            RightToolTabs().SelectedIndex(3);
        }
        else if (title == L"Output")
        {
            m_isBottomToolWindowHidden = false;
            BottomToolRow().Height({ 245, GridUnitType::Pixel });
            BottomToolTabs().SelectedIndex(1);
        }
        else if (title == L"Terminal")
        {
            m_isBottomToolWindowHidden = false;
            BottomToolRow().Height({ 245, GridUnitType::Pixel });
            BottomToolTabs().SelectedIndex(2);
        }
        else if (title == L"Error List")
        {
            m_isBottomToolWindowHidden = false;
            BottomToolRow().Height({ 245, GridUnitType::Pixel });
            BottomToolTabs().SelectedIndex(0);
            BottomToolWindowTitleText().Text(L"Error List - Current Project");
        }
    }

    void MainView::NavigateBack_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (CanGoBack())
        {
            GoBack();
        }
    }

    void MainView::NavigateForward_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (CanGoForward())
        {
            GoForward();
        }
    }

    void MainView::UnavailableMenu_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto item = sender.try_as<MenuFlyoutItem>())
        {
            SetOutput(L"View", std::wstring{ item.Text() } + L" is not implemented yet.");
        }
    }

    void MainView::DockCommand_Click(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto element = sender.try_as<FrameworkElement>();
        if (!element)
        {
            return;
        }

        auto const command = std::wstring{ unbox_value_or<hstring>(element.Tag(), L"") };
        if (command.rfind(L"float:", 0) == 0)
        {
            element.Tag(box_value(hstring{ command.substr(6) }));
            OpenFloatingToolWindow(element);
            return;
        }
        if (command == L"close-right" || command == L"autohide-right")
        {
            static constexpr wchar_t const* contentIds[] = { L"solutionExplorer", L"gitChanges", L"properties", L"diagnostics", L"documentOutline" };
            auto const index = static_cast<std::size_t>((std::max)(0, RightToolTabs().SelectedIndex()));
            if (m_viewModel && index < std::size(contentIds))
            {
                (void)winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(
                    contentIds[index],
                    command == L"close-right" ? ::VisualForge::IDE::Shell::DockCommandKind::Close : ::VisualForge::IDE::Shell::DockCommandKind::AutoHide);
            }
            m_isRightToolWindowHidden = true;
            RightToolWindowGrid().Visibility(Visibility::Collapsed);
            SetOutput(L"View", L"Right tool window hidden.");
            return;
        }
        if (command == L"close-left" || command == L"autohide-left")
        {
            LeftToolColumn().Width({ 0, GridUnitType::Pixel });
            SetOutput(L"View", L"Open Documents tool window hidden.");
            return;
        }
        if (command == L"close-bottom" || command == L"autohide-bottom")
        {
            static constexpr wchar_t const* contentIds[] = { L"errorList", L"output", L"terminal", L"diagnostics", L"diagnostics" };
            auto const index = static_cast<std::size_t>((std::max)(0, BottomToolTabs().SelectedIndex()));
            if (m_viewModel && index < std::size(contentIds))
            {
                (void)winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(
                    contentIds[index],
                    command == L"close-bottom" ? ::VisualForge::IDE::Shell::DockCommandKind::Close : ::VisualForge::IDE::Shell::DockCommandKind::AutoHide);
            }
            m_isBottomToolWindowHidden = true;
            BottomToolRow().Height({ 0, GridUnitType::Pixel });
            SetOutput(L"View", L"Bottom tool window hidden.");
            return;
        }
        if (command == L"maximize-bottom")
        {
            if (!m_isBottomToolWindowMaximized)
            {
                m_savedBottomToolHeight = BottomToolRow().Height().Value;
                BottomToolRow().Height({ 1, GridUnitType::Star });
                m_isBottomToolWindowMaximized = true;
            }
            else
            {
                BottomToolRow().Height({ m_savedBottomToolHeight > 0 ? m_savedBottomToolHeight : 245, GridUnitType::Pixel });
                m_isBottomToolWindowMaximized = false;
            }
        }
    }

    void MainView::FullScreen_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto window = winrt::VisualForge::implementation::App::window;
        if (!window)
        {
            return;
        }
        auto appWindow = window.AppWindow();
        if (!appWindow)
        {
            return;
        }

        m_isFullScreen = !m_isFullScreen;
        appWindow.SetPresenter(m_isFullScreen
                               ? winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen
                               : winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
        SetOutput(L"Window", m_isFullScreen ? L"Entered full screen." : L"Exited full screen.");
    }

    void MainView::OptionsMenu_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShowOptionsAsync();
    }

    void MainView::CommandPalette_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ShowCommandPaletteAsync();
    }

    void MainView::NuGetPackageManager_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"NuGet", L"Open a workspace before restoring packages.");
            return;
        }

        auto solution = FindWorkspaceFile(L".slnx");
        if (solution.empty())
        {
            SetOutput(L"NuGet", L"No .slnx solution was found in the workspace.");
            return;
        }

        ::VisualForge::Integration::NuGetAdapter adapter;
        RunGitCommandAsync(adapter.CreateRestoreCommand(solution), L"NuGet restore");
    }

    void MainView::VcpkgPackageManager_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"vcpkg", L"Open a workspace before installing packages.");
            return;
        }

        if (!std::filesystem::exists(m_workspaceRoot / L"vcpkg.json"))
        {
            SetOutput(L"vcpkg", L"No vcpkg.json manifest was found at the workspace root.");
            return;
        }

        ::VisualForge::Integration::VcpkgAdapter adapter;
        RunGitCommandAsync(adapter.CreateInstallCommand(m_workspaceRoot), L"vcpkg install");
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ShowCommandPaletteAsync()
    {
        std::vector<std::wstring> commands{
            L"New File",
            L"Open File",
            L"Open Workspace",
            L"Save All",
            L"Find in Files",
            L"Replace in Files",
            L"Go To Line",
            L"Build Solution",
            L"Rebuild Solution",
            L"Clean Solution",
            L"Build Startup Project",
            L"Start Debugging",
            L"Solution Explorer",
            L"Git Changes",
            L"Options" };

        StackPanel content;
        content.Spacing(8);
        AutoSuggestBox search;
        search.PlaceholderText(L"Type a command");
        content.Children().Append(search);
        ListView list;
        list.Height(320);
        content.Children().Append(list);

        auto populate = [&]
            {
                list.Items().Clear();
                auto query = std::wstring{ search.Text() };
                std::transform(query.begin(), query.end(), query.begin(), [](wchar_t value)
                               {
                                   return static_cast<wchar_t>(std::towlower(value));
                               });
                for (auto const& command : commands)
                {
                    auto searchable = command;
                    std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t value)
                                   {
                                       return static_cast<wchar_t>(std::towlower(value));
                                   });
                    if (!query.empty() && searchable.find(query) == std::wstring::npos)
                    {
                        continue;
                    }
                    auto item = ListViewItem{};
                    item.Content(box_value(hstring{ command }));
                    list.Items().Append(item);
                }
                if (list.Items().Size() > 0)
                {
                    list.SelectedIndex(0);
                }
            };
        search.TextChanged([&](AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
                           {
                               populate();
                           });
        populate();

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Command Palette"));
        dialog.Content(content);
        dialog.PrimaryButtonText(L"Execute");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto selected = list.SelectedItem().try_as<ListViewItem>();
        if (!selected)
        {
            co_return;
        }
        auto const command = std::wstring{ unbox_value<hstring>(selected.Content()) };
        if (command == L"New File")
        {
            co_await NewFileAsync();
        }
        else if (command == L"Open File")
        {
            co_await OpenFileAsync();
        }
        else if (command == L"Open Workspace")
        {
            co_await OpenWorkspaceAsync();
        }
        else if (command == L"Save All")
        {
            co_await SaveAllDocumentsAsync();
        }
        else if (command == L"Find in Files")
        {
            co_await SearchWorkspaceAsync();
        }
        else if (command == L"Replace in Files")
        {
            co_await ReplaceWorkspaceAsync();
        }
        else if (command == L"Go To Line")
        {
            co_await GoToLineAsync();
        }
        else if (command == L"Build Solution")
        {
            co_await BuildProjectAsync();
        }
        else if (command == L"Rebuild Solution")
        {
            co_await RunBuildAsync(L"Rebuild", L"Rebuild solution", false);
        }
        else if (command == L"Clean Solution")
        {
            co_await RunBuildAsync(L"Clean", L"Clean solution", false);
        }
        else if (command == L"Build Startup Project")
        {
            co_await RunBuildAsync(L"Build", L"Build startup project", true);
        }
        else if (command == L"Start Debugging")
        {
            RunProject_Click(*this, RoutedEventArgs{});
        }
        else if (command == L"Solution Explorer")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            RightToolTabs().SelectedIndex(0);
        }
        else if (command == L"Git Changes")
        {
            m_isRightToolWindowHidden = false;
            RightToolWindowGrid().Visibility(Visibility::Visible);
            RightToolTabs().SelectedIndex(1);
        }
        else if (command == L"Options")
        {
            co_await ShowOptionsAsync();
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ShowOptionsAsync()
    {
        auto& database = Core::AppSettingsDatabase::Instance();
        StackPanel content;
        content.Spacing(10);

        TextBlock clangdLabel;
        clangdLabel.Text(L"clangd executable");
        content.Children().Append(clangdLabel);
        TextBox clangdBox;
        clangdBox.Text(hstring{ database.GetStringW(Core::AppSettingsDatabase::CAT_UI, "tools.clangd.path").value_or(L"clangd.exe") });
        clangdBox.PlaceholderText(L"clangd.exe");
        content.Children().Append(clangdBox);

        TextBlock dapLabel;
        dapLabel.Text(L"DAP executable");
        content.Children().Append(dapLabel);
        TextBox dapBox;
        dapBox.Text(hstring{ database.GetStringW(Core::AppSettingsDatabase::CAT_UI, "tools.dap.path").value_or(L"lldb-dap.exe") });
        dapBox.PlaceholderText(L"lldb-dap.exe");
        content.Children().Append(dapBox);

        TextBlock themeLabel;
        themeLabel.Text(L"Theme");
        content.Children().Append(themeLabel);
        ComboBox themeBox;
        themeBox.Items().Append(box_value(L"Default"));
        themeBox.Items().Append(box_value(L"Dark"));
        themeBox.Items().Append(box_value(L"Light"));
        themeBox.SelectedIndex(this->RequestedTheme() == ElementTheme::Dark ? 1
                               : this->RequestedTheme() == ElementTheme::Light ? 2 : 0);
        content.Children().Append(themeBox);

        CheckBox prepareBox;
        prepareBox.Content(box_value(L"Prepare clangd when a workspace opens"));
        prepareBox.IsChecked(database.GetBool(Core::AppSettingsDatabase::CAT_UI, "tools.clangd.auto_prepare").value_or(true));
        content.Children().Append(prepareBox);

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Options"));
        dialog.Content(content);
        dialog.PrimaryButtonText(L"Save");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        database.SetStringW(Core::AppSettingsDatabase::CAT_UI, "tools.clangd.path", std::wstring{ clangdBox.Text() });
        database.SetStringW(Core::AppSettingsDatabase::CAT_UI, "tools.dap.path", std::wstring{ dapBox.Text() });
        database.SetBool(Core::AppSettingsDatabase::CAT_UI, "tools.clangd.auto_prepare", prepareBox.IsChecked().Value());
        switch (themeBox.SelectedIndex())
        {
            case 1:
                database.SetString(Core::AppSettingsDatabase::CAT_UI, "ui.theme", "dark");
                this->RequestedTheme(ElementTheme::Dark);
                break;
            case 2:
                database.SetString(Core::AppSettingsDatabase::CAT_UI, "ui.theme", "light");
                this->RequestedTheme(ElementTheme::Light);
                break;
            default:
                database.SetString(Core::AppSettingsDatabase::CAT_UI, "ui.theme", "default");
                this->RequestedTheme(ElementTheme::Default);
                break;
        }
        SetOutput(L"Options", L"Settings saved.");
        if (m_workspaceRoot.empty() || !database.GetBool(Core::AppSettingsDatabase::CAT_UI, "tools.clangd.auto_prepare").value_or(true))
        {
            co_return;
        }
        m_languageService.Stop();
        PrepareLanguageServiceAsync();
    }

    void MainView::AboutMenu_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StackPanel content;
        content.Spacing(8);
        auto appName = TextBlock{};
        appName.Text(L"VisualForge");
        appName.FontSize(22);
        content.Children().Append(appName);
        auto description = TextBlock{};
        description.Text(L"WinUI 3 C++/WinRT IDE prototype");
        description.Opacity(0.8);
        content.Children().Append(description);
        auto workspace = TextBlock{};
        workspace.Text(hstring{ L"Workspace: " + (m_solutionPath.empty() ? L"No solution loaded" : m_solutionPath.filename().wstring()) });
        content.Children().Append(workspace);

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"About VisualForge"));
        dialog.Content(content);
        dialog.CloseButtonText(L"Close");
        (void)dialog.ShowAsync();
    }

    void MainView::WorkspaceModeComboBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        // SelectionChanged can be raised while XAML is still connecting named
        // elements. Layout rows are valid only after Page_Loaded completes.
        if (!m_isPageInitialized || m_isApplyingWorkspaceMode)
        {
            return;
        }

        auto const mode = WorkspaceModeComboBox().SelectedIndex() == 1
            ? ::VisualForge::IDE::Shell::WorkspaceMode::Debugging
            : ::VisualForge::IDE::Shell::WorkspaceMode::Editing;
        ApplyWorkspaceMode(mode, true);
    }

    void MainView::EditingLayout_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Editing, true);
    }

    void MainView::DebuggingLayout_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
    }

    void MainView::ResetWindowLayout_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_viewModel)
        {
            return;
        }

        auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
        viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing) = { 245, 0, 1, false, 250, 360 };
        viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging) = { 320, 3, 3, true, 250, 360 };
        ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
        SetOutput(L"Window", L"Window layouts reset to defaults.");
    }

    void MainView::SaveWindowLayout_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_viewModel)
        {
            auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
            ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
            SetOutput(L"Window", L"Editing and debugging layouts saved.");
        }
    }

    void MainView::LayoutSplitter_PointerReleased(
        IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_viewModel || m_isApplyingWorkspaceMode)
        {
            return;
        }

        auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
        auto& profile = viewModel->Workspace().Profile(viewModel->Workspace().Mode());
        profile.BottomToolHeight = (std::clamp)(static_cast<int>(BottomToolRow().ActualHeight()), 220, 520);
        profile.LeftToolWidth = (std::clamp)(static_cast<int>(LeftToolColumn().ActualWidth()), 210, 520);
        profile.RightToolWidth = (std::clamp)(static_cast<int>(RightToolColumn().ActualWidth()), 300, 620);
        ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
        args.Handled(true);
    }

    void MainView::BuildConfiguration_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!m_isPageInitialized || m_isLoadingProjectConfigurations)
        {
            return;
        }
        if (auto const item = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>())
        {
            Core::AppSettingsDatabase::Instance().SetString(
                Core::AppSettingsDatabase::CAT_UI,
                "build.configuration",
                winrt::to_string(unbox_value<hstring>(item.Content())));
            UpdatePropertiesPanel();
        }
    }

    void MainView::BuildPlatform_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!m_isPageInitialized || m_isLoadingProjectConfigurations)
        {
            return;
        }
        if (auto const item = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>())
        {
            Core::AppSettingsDatabase::Instance().SetString(
                Core::AppSettingsDatabase::CAT_UI,
                "build.platform",
                winrt::to_string(unbox_value<hstring>(item.Content())));
            UpdatePropertiesPanel();
        }
    }

    void MainView::StartDebuggingKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode::Debugging, true);
        RunProject_Click(*this, RoutedEventArgs{});
    }

    void MainView::BuildSolutionKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        BuildProjectAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenFileAsync()
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        try
        {
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::List);
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");
            auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
            auto file = co_await picker.PickSingleFileAsync();
            if (!file)
            {
                co_return;
            }

            co_await OpenStorageFileAsync(file);
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Editor", L"Open failed: " + std::wstring{ error.message() });
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenWorkspaceAsync()
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        try
        {
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::List);
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L".slnx");
            picker.FileTypeFilter().Append(L".sln");
            auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
            auto solution = co_await picker.PickSingleFileAsync();
            if (!solution)
            {
                co_return;
            }

            co_await OpenWorkspaceFileAsync(solution);
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Workspace", L"Open failed: " + std::wstring{ error.message() });
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenWorkspaceFileAsync(StorageFile const& solution)
    {
        SaveEditorSession();
        SaveBreakpoints();
        if (!solution || !co_await CloseDocumentsForWorkspaceSwitchAsync())
        {
            co_return;
        }

        m_solutionPath = solution.Path().c_str();
        m_workspaceRoot = m_solutionPath.parent_path();
        m_navigationHistory.clear();
        m_navigationIndex = 0;
        UpdateNavigationState(false, false);
        RecordRecentWorkspace(m_solutionPath);
        LoadBreakpoints();
        auto const workspaceSettingsKey = winrt::to_string(hstring{ m_solutionPath.wstring() });
        m_showAllSolutionFiles = Core::AppSettingsDatabase::Instance().GetBool(
            Core::AppSettingsDatabase::CAT_UI,
            "workspace.solutionExplorer.showAll." + workspaceSettingsKey).value_or(false);
        m_solutionExplorerFilter = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI,
            "workspace.solutionExplorer.filter." + workspaceSettingsKey).value_or(L"");
        auto const solutionProjects = ReadSolutionProjectPaths(m_solutionPath);
        m_startupProjectPath.clear();
        auto savedStartupProject = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI, "startup.project").value_or(L"");
        if (!savedStartupProject.empty())
        {
            std::error_code startupError;
            auto const candidate = std::filesystem::path{ savedStartupProject };
            auto const relative = std::filesystem::relative(candidate, m_workspaceRoot, startupError);
            auto const normalizedCandidate = candidate.lexically_normal();
            if (!startupError && !relative.empty() && relative.native().find(L"..") != 0
                && std::find(solutionProjects.begin(), solutionProjects.end(), normalizedCandidate) != solutionProjects.end())
            {
                m_startupProjectPath = normalizedCandidate;
            }
        }
        if (m_startupProjectPath.empty())
        {
            m_startupProjectPath = solutionProjects.empty() ? std::filesystem::path{} : solutionProjects.front();
        }
        SolutionExplorer().SetStartupProject(hstring{ m_startupProjectPath.wstring() });
        ++m_languagePreparationGeneration;
        m_isPreparingLanguageService = false;
        m_languageService.Stop();
        m_diagnosticsByPath.clear();
        m_diagnosticEntries.clear();
        DiagnosticsList().Items().Clear();
        for (auto const& [_, state] : m_documentTabs)
        {
            state.Editor.ClearDiagnosticLines();
        }
        LoadProjectConfigurations();
        PopulateWorkspaceTree();
        co_await RestoreEditorSessionAsync();
        if (Core::AppSettingsDatabase::Instance().GetBool(
            Core::AppSettingsDatabase::CAT_UI, "tools.clangd.auto_prepare").value_or(true))
        {
            PrepareLanguageServiceAsync();
        }
        SetOutput(L"Workspace", L"Opened " + m_solutionPath.wstring());
        ShellStatusText().Text(L"Workspace loaded");
        RefreshGitChangesAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainView::NewProjectAsync()
    {
        try
        {
            StackPanel content;
            content.Spacing(8);
            content.Children().Append(TextBlock{ nullptr });
            content.Children().GetAt(0).as<TextBlock>().Text(L"Project name");
            TextBox nameBox;
            nameBox.PlaceholderText(L"VisualForgeSample");
            content.Children().Append(nameBox);

            ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(box_value(L"Create a new project"));
            dialog.Content(content);
            dialog.PrimaryButtonText(L"Create");
            dialog.CloseButtonText(L"Cancel");
            if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
            {
                co_return;
            }

            auto projectName = nameBox.Text();
            if (projectName.empty())
            {
                SetOutput(L"Project", L"Project name cannot be empty.");
                co_return;
            }
            for (auto const character : projectName)
            {
                if (!(std::iswalnum(character) || character == L'_' || character == L'-'))
                {
                    SetOutput(L"Project", L"Use only letters, numbers, '_' or '-' in the project name.");
                    co_return;
                }
            }
            const auto projectNameUtf8 = winrt::to_string(projectName);

            FolderPicker picker;
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");
            auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
            auto parent = co_await picker.PickSingleFolderAsync();
            if (!parent)
            {
                co_return;
            }

            const std::filesystem::path root = std::filesystem::path(parent.Path().c_str()) / projectName.c_str();
            if (std::filesystem::exists(root))
            {
                SetOutput(L"Project", L"The project folder already exists.");
                co_return;
            }
            std::filesystem::create_directories(root);
            const auto projectFile = root / (std::wstring{ projectName } + L".vcxproj");
            const auto solutionFile = root / (std::wstring{ projectName } + L".slnx");
            const auto sourceFile = root / "main.cpp";

            std::ofstream solutionStream(solutionFile, std::ios::binary);
            solutionStream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Solution>\n  <Project Path=\"" << projectNameUtf8 << ".vcxproj\" />\n</Solution>\n";
            std::ofstream projectStream(projectFile, std::ios::binary);
            projectStream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
                "  <ItemGroup Label=\"ProjectConfigurations\">\n"
                "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>\n"
                "    <ProjectConfiguration Include=\"Release|x64\"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>\n"
                "  </ItemGroup>\n"
                "  <PropertyGroup Label=\"Globals\"><ProjectGuid>{00000000-0000-0000-0000-000000000001}</ProjectGuid><Keyword>Win32Proj</Keyword><RootNamespace>" << projectNameUtf8 << "</RootNamespace></PropertyGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
                "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\" Label=\"Configuration\"><ConfigurationType>Application</ConfigurationType><UseDebugLibraries>true</UseDebugLibraries><PlatformToolset>v143</PlatformToolset><CharacterSet>Unicode</CharacterSet></PropertyGroup>\n"
                "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\" Label=\"Configuration\"><ConfigurationType>Application</ConfigurationType><UseDebugLibraries>false</UseDebugLibraries><PlatformToolset>v143</PlatformToolset><WholeProgramOptimization>true</WholeProgramOptimization><CharacterSet>Unicode</CharacterSet></PropertyGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
                "  <ItemGroup><ClCompile Include=\"main.cpp\" /></ItemGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n</Project>\n";
            std::ofstream sourceStream(sourceFile, std::ios::binary);
            sourceStream << "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello from VisualForge.\" << std::endl;\n    return 0;\n}\n";
            if (!solutionStream || !projectStream || !sourceStream)
            {
                SetOutput(L"Project", L"Could not write the new project files.");
                co_return;
            }

            SetOutput(L"Project", L"Created " + solutionFile.wstring());
            co_await OpenWorkspaceFileAsync(co_await StorageFile::GetFileFromPathAsync(solutionFile.wstring()));
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Project", std::wstring{ L"New project failed: " } + winrt::to_hstring(error.what()).c_str());
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Project", L"New project failed: " + std::wstring{ error.message() });
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MainView::CloseDocumentsForWorkspaceSwitchAsync()
    {
        std::vector<TabViewItem> tabs;
        std::vector<TabViewItem> dirtyTabs;
        for (auto const& [_, state] : m_documentTabs)
        {
            tabs.push_back(state.Tab);
            if (state.IsDirty)
            {
                dirtyTabs.push_back(state.Tab);
            }
        }

        if (!dirtyTabs.empty())
        {
            ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(box_value(L"Close current workspace"));
            dialog.Content(box_value(L"Save changes before opening another workspace?"));
            dialog.PrimaryButtonText(L"Save All");
            dialog.SecondaryButtonText(L"Discard All");
            dialog.CloseButtonText(L"Cancel");
            auto const result = co_await dialog.ShowAsync();
            if (result == ContentDialogResult::None)
            {
                co_return false;
            }
            if (result == ContentDialogResult::Primary)
            {
                for (auto const& tab : dirtyTabs)
                {
                    co_await SaveDocumentAsync(tab);
                }
                for (auto const& [_, state] : m_documentTabs)
                {
                    if (state.IsDirty)
                    {
                        SetOutput(L"Workspace", L"Workspace switch cancelled because a document could not be saved.");
                        co_return false;
                    }
                }
            }
        }

        for (auto const& tab : tabs)
        {
            CloseDocument(tab);
        }
        co_return true;
    }

    winrt::Windows::Foundation::IAsyncAction MainView::PrepareLanguageServiceAsync()
    {
        if (m_isPreparingLanguageService || m_workspaceRoot.empty() || m_solutionPath.empty())
        {
            co_return;
        }

        m_isPreparingLanguageService = true;
        auto const preparationGeneration = m_languagePreparationGeneration;
        LanguageServiceStatusText().Text(L"clangd: preparing compile database");
        SetOutput(L"MSBuild", L"Generating compile_commands.json for clangd...");
        auto const weakThis = get_weak();
        auto const queue = DispatcherQueue();
        auto const root = m_workspaceRoot;
        auto const solution = m_solutionPath;
        auto const configurationItem = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>();
        auto const platformItem = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>();
        auto const configuration = configurationItem ? std::wstring{ unbox_value<hstring>(configurationItem.Content()) } : L"Debug";
        auto const platform = platformItem ? std::wstring{ unbox_value<hstring>(platformItem.Content()) } : L"x64";
        co_await winrt::resume_background();

        ::VisualForge::Integration::ProjectContext context;
        context.WorkspaceRoot = root;
        context.SolutionPath = solution;
        context.Configuration = configuration;
        context.Platform = platform;
        ::VisualForge::Integration::MSBuildAdapter adapter;
        auto command = adapter.CreateGenerateCompileCommandsCommand(context);
        ::VisualForge::Tool::ProcessSession process;
        auto const started = process.Start(command);
        if (started)
        {
            (void)process.WaitForExit();
        }
        auto const snapshot = process.Snapshot();
        auto output = snapshot.StdOut + snapshot.StdErr;
        if (output.size() > 12000)
        {
            output.erase(0, output.size() - 12000);
        }
        (void)queue.TryEnqueue([weakThis, preparationGeneration, started, exitCode = snapshot.ExitCode, output = std::move(output)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }
                                   if (preparationGeneration != strongThis->m_languagePreparationGeneration)
                                   {
                                       return;
                                   }

                                   strongThis->m_isPreparingLanguageService = false;
                                   if (started && exitCode == 0)
                                   {
                                       strongThis->SetOutput(L"MSBuild", L"compile_commands.json generated.");
                                   }
                                   else
                                   {
                                       strongThis->SetOutput(L"MSBuild", started
                                                             ? L"compile_commands.json generation failed; starting clangd without it."
                                                             : L"MSBuild could not be started; starting clangd without compile_commands.json.");
                                   }
                                   strongThis->StartLanguageService();
                               });
    }

    void MainView::StartLanguageService()
    {
        if (m_workspaceRoot.empty())
        {
            return;
        }

        auto const clangdPath = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI, "tools.clangd.path").value_or(L"clangd.exe");
        if (m_viewModel)
        {
            winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkLanguageServiceStarting();
        }
        m_pendingSemanticTokenRequests.clear();
        m_languageService.Start(clangdPath, m_workspaceRoot, m_workspaceRoot);
        if (!m_languageServiceTimer)
        {
            auto weakThis = get_weak();
            m_languageServiceTimer = DispatcherQueue().CreateTimer();
            m_languageServiceTimer.Interval(Windows::Foundation::TimeSpan{ 1000000 });
            m_languageServiceTimer.Tick([weakThis](IInspectable const&, IInspectable const&)
                                        {
                                            if (auto strongThis = weakThis.get())
                                            {
                                                strongThis->PollLanguageService();
                                                strongThis->PollDebugAdapter();
                                                strongThis->PollTerminal();
                                                strongThis->PollExternalChanges();
                                            }
                                        });
            m_languageServiceTimer.Start();
        }

        if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            if (m_viewModel)
            {
                winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkLanguageServiceReady();
            }
            m_languageServiceRestartCooldown = 0;
            for (auto const& [key, state] : m_documentTabs)
            {
                if (!state.Path.empty())
                {
                    m_languageService.DidOpenFile(
                        ToFileUri(state.Path),
                        LanguageIdForPath(state.Path),
                        state.LastText.empty() ? std::wstring{ state.Editor.Text() } : state.LastText);
                    m_pendingSemanticTokenRequests.emplace(
                        m_languageService.RequestSemanticTokens(ToFileUri(state.Path)), key);
                }
            }
            LanguageServiceStatusText().Text(L"clangd: running");
            SetOutput(L"clangd", L"Language server started.");
        }
        else
        {
            m_languageServiceRestartCooldown = 10;
            LanguageServiceStatusText().Text(L"clangd: unavailable");
            SetOutput(L"clangd", L"clangd.exe could not be started.");
        }
    }

    void MainView::PollDebugAdapter()
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            return;
        }

        auto messages = m_debugAdapter.DrainProtocolMessages();
        if (messages.empty())
        {
            return;
        }

        if (m_viewModel)
        {
            winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().ApplyDapMessages(messages);
        }
        bool sessionEnded = false;
        for (auto const& message : messages)
        {
            std::wstring label;
            if (!message.Summary.Event.empty())
            {
                label = L"event: " + message.Summary.Event;
            }
            else if (!message.Summary.Command.empty())
            {
                label = L"response: " + message.Summary.Command;
            }
            if (!label.empty())
            {
                auto item = ListViewItem{};
                item.Content(box_value(hstring{ label }));
                DebugEventsList().Items().Append(item);
            }
            if (message.Summary.Event == L"stopped")
            {
                if (message.ThreadId > 0)
                {
                    m_debugThreadId = message.ThreadId;
                }
                m_debugAdapter.RequestStackTrace(m_debugThreadId);
            }
            if (message.Summary.Event == L"exited" || message.Summary.Event == L"terminated")
            {
                sessionEnded = true;
            }
            if (message.Summary.Event == L"continued")
            {
                m_selectedFrameId = 0;
                LocalsTree().RootNodes().Clear();
                m_pendingVariableRequests.clear();
                for (auto& [_, state] : m_documentTabs)
                {
                    state.Editor.SetDebugLine(0);
                }
            }
            if (!message.StackFrames.empty())
            {
                if (m_selectedFrameId == 0)
                {
                    m_selectedFrameId = message.StackFrames.front().Id;
                }
                CallStackList().Items().Clear();
                m_stackFrames.clear();
                for (auto& [_, state] : m_documentTabs)
                {
                    state.Editor.SetDebugLine(0);
                }
                for (auto const& frame : message.StackFrames)
                {
                    auto item = ListViewItem{};
                    item.Content(box_value(hstring{
                        frame.Name + L"  [" + std::to_wstring(frame.Line) + L"]" }));
                    m_stackFrames.emplace(winrt::get_abi(item), frame);
                    CallStackList().Items().Append(item);
                    for (auto& [_, state] : m_documentTabs)
                    {
                        if (frame.Id == m_selectedFrameId && state.Path == std::filesystem::path{ frame.SourcePath })
                        {
                            state.Editor.SetDebugLine(frame.Line);
                        }
                    }
                }
                m_debugAdapter.RequestScopes(m_selectedFrameId);
            }
            if (!message.Scopes.empty())
            {
                LocalsTree().RootNodes().Clear();
                m_pendingVariableRequests.clear();
                m_variableReferences.clear();
                m_loadedVariableNodes.clear();
                for (auto const& scope : message.Scopes)
                {
                    auto scopeNode = TreeViewNode{};
                    scopeNode.Content(box_value(hstring{ scope.Name }));
                    LocalsTree().RootNodes().Append(scopeNode);
                    if (scope.VariablesReference != 0)
                    {
                        scopeNode.Children().Append(TreeViewNode{});
                        m_variableReferences.emplace(winrt::get_abi(scopeNode), scope.VariablesReference);
                    }
                }
            }
            if (!message.Variables.empty())
            {
                if (m_pendingVariableRequests.empty())
                {
                    continue;
                }
                auto pending = m_pendingVariableRequests.front();
                m_pendingVariableRequests.erase(m_pendingVariableRequests.begin());
                pending.Node.Children().Clear();
                for (auto const& variable : message.Variables)
                {
                    auto display = variable.Name + L" = " + variable.Value;
                    if (!variable.Type.empty())
                    {
                        display += L"  (" + variable.Type + L")";
                    }
                    auto item = TreeViewNode{};
                    item.Content(box_value(hstring{ display }));
                    if (variable.VariablesReference != 0)
                    {
                        item.Children().Append(TreeViewNode{});
                        m_variableReferences.emplace(winrt::get_abi(item), variable.VariablesReference);
                    }
                    pending.Node.Children().Append(item);
                }
            }
        }
        if (sessionEnded)
        {
            m_debugAdapter.Finish();
            m_selectedFrameId = 0;
            m_debugThreadId = 1;
            m_stackFrames.clear();
            m_pendingVariableRequests.clear();
            m_variableReferences.clear();
            m_loadedVariableNodes.clear();
            LocalsTree().RootNodes().Clear();
            CallStackList().Items().Clear();
            for (auto& [_, state] : m_documentTabs)
            {
                state.Editor.SetDebugLine(0);
            }
            if (m_viewModel)
            {
                winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkDebugStopped();
                DebugSessionText().Text(m_viewModel.DebugStatus());
            }
            else
            {
                DebugSessionText().Text(L"Debug session ended.");
            }
            ShellStatusText().Text(L"Ready");
            SetOutput(L"Debug", L"Debug session ended.");
        }
        if (m_viewModel)
        {
            DebugSessionText().Text(m_viewModel.DebugStatus());
        }
        SetOutput(L"DAP", L"Debugger protocol events received.");
        RefreshStatusBar();
    }

    void MainView::PollLanguageService()
    {
        if (m_workspaceRoot.empty() || m_isPreparingLanguageService
            || m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Stopped)
        {
            return;
        }

        if (m_languageServiceRestartCooldown > 0)
        {
            --m_languageServiceRestartCooldown;
            return;
        }

        auto const processSnapshot = m_languageService.ProcessSnapshot();
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running
            || processSnapshot.State != ::VisualForge::Tool::ProcessSessionState::Running)
        {
            if (!Core::AppSettingsDatabase::Instance().GetBool(
                Core::AppSettingsDatabase::CAT_UI, "tools.clangd.auto_prepare").value_or(true))
            {
                return;
            }

            m_languageService.Stop();
            if (m_viewModel)
            {
                winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().MarkLanguageServiceStopped();
            }
            LanguageServiceStatusText().Text(L"clangd: restarting");
            SetOutput(L"clangd", L"Language server process exited; attempting recovery.");
            StartLanguageService();
            if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
            {
                m_languageServiceRestartCooldown = 10;
            }
            return;
        }

        auto messages = m_languageService.DrainProtocolMessages();
        if (m_viewModel)
        {
            winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().ApplyLspMessages(messages);
        }
        for (auto const& message : messages)
        {
            if (message.Summary.Id)
            {
                if (auto pending = m_pendingSemanticTokenRequests.find(*message.Summary.Id);
                    pending != m_pendingSemanticTokenRequests.end())
                {
                    auto const key = pending->second;
                    m_pendingSemanticTokenRequests.erase(pending);
                    if (auto state = m_documentTabs.find(key); state != m_documentTabs.end())
                    {
                        std::wstring encoded;
                        for (auto const& token : message.SemanticTokens)
                        {
                            if (!encoded.empty()) encoded += L'|';
                            encoded += std::to_wstring(token.Line) + L',' + std::to_wstring(token.Column)
                                + L',' + std::to_wstring(token.Length) + L',' + std::to_wstring(token.TokenType);
                        }
                        state->second.Editor.SetSemanticTokens(hstring{ encoded });
                    }
                    continue;
                }
            }
            if (message.Summary.Id && *message.Summary.Id == m_pendingDocumentSymbolsRequestId)
            {
                m_pendingDocumentSymbolsRequestId = -1;
                auto const key = m_pendingDocumentSymbolsDocumentKey;
                m_pendingDocumentSymbolsDocumentKey = nullptr;
                DocumentOutlineList().Items().Clear();
                m_documentSymbolEntries.clear();
                for (auto const& symbol : message.DocumentSymbols)
                {
                    auto item = ListViewItem{};
                    item.Content(box_value(hstring{
                        (symbol.Kind == 5 ? L"class  " : symbol.Kind == 12 ? L"function  " : L"symbol  ")
                        + symbol.Name
                        + (symbol.Detail.empty() ? L"" : L"  " + symbol.Detail) }));
                    m_documentSymbolEntries.emplace(winrt::get_abi(item), symbol.Location);
                    DocumentOutlineList().Items().Append(item);
                }
                if (message.DocumentSymbols.empty())
                {
                    SetOutput(L"clangd", L"No document symbols found.");
                }
                else
                {
                    SetOutput(L"clangd", L"Document outline updated.");
                }
                (void)key;
                continue;
            }
            if (message.Summary.Id && *message.Summary.Id == m_pendingFormattingRequestId)
            {
                m_pendingFormattingRequestId = -1;
                auto const key = m_pendingFormattingDocumentKey;
                m_pendingFormattingDocumentKey = nullptr;
                auto found = m_documentTabs.find(key);
                if (found == m_documentTabs.end())
                {
                    continue;
                }
                auto document = m_documentManager.Find(found->second.DocumentKey);
                if (!document || message.TextEdits.empty())
                {
                    SetOutput(L"clangd", message.TextEdits.empty() ? L"Document is already formatted." : L"Formatting document failed.");
                    continue;
                }

                auto const previous = std::wstring{ document->Buffer().CreateSnapshot().Text() };
                auto offsetFromPosition = [](std::wstring_view text, std::size_t line, std::size_t column)
                    {
                        std::size_t currentLine{};
                        std::size_t lineStart{};
                        while (currentLine < line && lineStart < text.size())
                        {
                            auto const newline = text.find(L'\n', lineStart);
                            if (newline == std::wstring_view::npos)
                            {
                                lineStart = text.size();
                                break;
                            }
                            lineStart = newline + 1;
                            ++currentLine;
                        }
                        auto const lineEnd = text.find(L'\n', lineStart);
                        return (std::min)(lineStart + column, lineEnd == std::wstring_view::npos ? text.size() : lineEnd);
                    };
                auto edits = message.TextEdits;
                std::sort(edits.begin(), edits.end(), [&](auto const& left, auto const& right)
                          {
                              return offsetFromPosition(previous, left.Location.Line, left.Location.Column)
                        > offsetFromPosition(previous, right.Location.Line, right.Location.Column);
                          });
                for (auto const& edit : edits)
                {
                    auto const start = offsetFromPosition(previous, edit.Location.Line, edit.Location.Column);
                    auto const end = offsetFromPosition(previous, edit.Location.EndLine, edit.Location.EndColumn);
                    if (start <= end && end <= previous.size())
                    {
                        document->Delete(start, end - start);
                        document->Insert(start, edit.NewText);
                    }
                }
                SynchronizeDocumentFromModel(key, previous);
                SetOutput(L"clangd", L"Document formatted.");
                continue;
            }
            if (message.Summary.Id && *message.Summary.Id == m_pendingCodeActionRequestId)
            {
                m_pendingCodeActionRequestId = -1;
                if (message.CodeActions.empty())
                {
                    SetOutput(L"clangd", L"No quick actions are available at the current position.");
                }
                else
                {
                    ShowCodeActionsAsync(message.CodeActions);
                }
                continue;
            }
            if (message.Summary.Method == L"textDocument/publishDiagnostics")
            {
                auto const diagnosticUri = ::VisualForge::EditorCore::Protocol::JsonMessageSummaryParser::ReadStringField(
                    message.Payload, "uri").value_or(L"");
                if (!diagnosticUri.empty())
                {
                    auto& diagnostics = m_diagnosticsByPath[PathFromFileUri(diagnosticUri).wstring()];
                    diagnostics.erase(std::remove_if(diagnostics.begin(), diagnostics.end(), [](DiagnosticEntry const& entry)
                                                     {
                                                         return !entry.IsBuildDiagnostic;
                                                     }), diagnostics.end());
                }
                for (auto const& diagnostic : message.Diagnostics)
                {
                    auto const path = PathFromFileUri(diagnostic.Uri);
                    auto const severity = diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Error ? L"error"
                        : diagnostic.Severity == ::VisualForge::EditorCore::LSP::LspDiagnosticSeverity::Warning ? L"warning" : L"info";
                    m_diagnosticsByPath[path.wstring()].push_back({
                        path,
                        diagnostic.Line + 1,
                        diagnostic.Column + 1,
                        std::wstring{ severity },
                        diagnostic.Code,
                        diagnostic.Message,
                        false });
                }
                RebuildDiagnosticsList();
                SetOutput(L"clangd", L"Diagnostics updated.");
            }
            else if (message.Summary.Method.empty() && message.Payload.find("\"items\"") != std::string::npos)
            {
                CompletionListView().Items().Clear();
                m_completionEntries.clear();
                for (auto const& completion : message.Completions)
                {
                    auto item = ListViewItem{};
                    item.Content(box_value(hstring{
                        completion.Label + (completion.Detail.empty() ? L"" : L"  " + completion.Detail) }));
                    m_completionEntries.emplace(
                        winrt::get_abi(item),
                        CompletionEntry{ completion.Label, completion.Detail, completion.InsertText });
                    CompletionListView().Items().Append(item);
                }
                if (!message.Completions.empty())
                {
                    CompletionListView().SelectedIndex(0);
                    CompletionPopup().IsOpen(true);
                    CompletionListView().Focus(FocusState::Programmatic);
                }
                else
                {
                    CompletionPopup().IsOpen(false);
                }
            }
            else if (message.Summary.Id && *message.Summary.Id == m_pendingHoverRequestId)
            {
                m_pendingHoverRequestId = -1;
                HoverTextBlock().Text(hstring{ message.HoverText.empty() ? L"No hover information." : message.HoverText });
                HoverPopup().IsOpen(true);
            }
            else if (message.Summary.Id && *message.Summary.Id == m_pendingDefinitionRequestId)
            {
                m_pendingDefinitionRequestId = -1;
                if (message.Locations.empty())
                {
                    SetOutput(L"clangd", L"Definition not found.");
                }
                else
                {
                    auto const& location = message.Locations.front();
                    OpenDiagnosticAsync(
                        PathFromFileUri(location.Uri),
                        location.Line + 1,
                        location.Column + 1);
                }
            }
            else if (message.Summary.Id && *message.Summary.Id == m_pendingReferencesRequestId)
            {
                m_pendingReferencesRequestId = -1;
                SearchResultsList().Items().Clear();
                m_workspaceSearchResults.clear();
                for (auto const& location : message.Locations)
                {
                    auto const path = PathFromFileUri(location.Uri);
                    auto item = ListViewItem{};
                    item.Content(box_value(hstring{
                        L"reference  " + path.filename().wstring()
                        + L":" + std::to_wstring(location.Line + 1)
                        + L":" + std::to_wstring(location.Column + 1) }));
                    m_workspaceSearchResults.emplace(winrt::get_abi(item), WorkspaceSearchResult{
                        path, location.Line + 1, location.Column + 1, L"clangd reference" });
                    SearchResultsList().Items().Append(item);
                }
                DiagnosticsTabs().SelectedIndex(1);
                BottomToolRow().Height({ 245, GridUnitType::Pixel });
                if (message.Locations.empty())
                {
                    SetOutput(L"clangd", L"No references found.");
                }
                else
                {
                    SetOutput(L"clangd", L"Found " + std::to_wstring(message.Locations.size()) + L" references.");
                }
            }
            else if (message.Summary.Id && *message.Summary.Id == m_pendingRenameRequestId)
            {
                m_pendingRenameRequestId = -1;
                if (message.TextEdits.empty())
                {
                    SetOutput(L"clangd", L"Rename was not available at the current position.");
                    continue;
                }

                auto offsetFromPosition = [](std::wstring_view text, std::size_t line, std::size_t column)
                    {
                        std::size_t currentLine{};
                        std::size_t lineStart{};
                        while (currentLine < line && lineStart < text.size())
                        {
                            auto const newline = text.find(L'\n', lineStart);
                            if (newline == std::wstring_view::npos)
                            {
                                lineStart = text.size();
                                break;
                            }
                            lineStart = newline + 1;
                            ++currentLine;
                        }
                        auto const lineEnd = text.find(L'\n', lineStart);
                        return (std::min)(lineStart + column, lineEnd == std::wstring_view::npos ? text.size() : lineEnd);
                    };

                std::unordered_map<std::wstring, std::vector<::VisualForge::EditorCore::LSP::LspTextEdit>> editsByPath;
                for (auto const& edit : message.TextEdits)
                {
                    auto const path = PathFromFileUri(edit.Location.Uri).lexically_normal();
                    if (!path.empty() && std::filesystem::exists(path))
                    {
                        editsByPath[path.wstring()].push_back(edit);
                    }
                }

                std::size_t applied{};
                for (auto& [pathText, edits] : editsByPath)
                {
                    auto const path = std::filesystem::path{ pathText };
                    auto* document = m_documentManager.Find(path);
                    if (!document)
                    {
                        document = &m_documentManager.Open(path);
                    }
                    auto const previous = std::wstring{ document->Buffer().CreateSnapshot().Text() };
                    std::sort(edits.begin(), edits.end(), [&](auto const& left, auto const& right)
                              {
                                  return offsetFromPosition(previous, left.Location.Line, left.Location.Column)
                        > offsetFromPosition(previous, right.Location.Line, right.Location.Column);
                              });

                    for (auto const& edit : edits)
                    {
                        auto const start = offsetFromPosition(previous, edit.Location.Line, edit.Location.Column);
                        auto const end = offsetFromPosition(previous, edit.Location.EndLine, edit.Location.EndColumn);
                        if (start > end || end > previous.size())
                        {
                            continue;
                        }
                        document->Delete(start, end - start);
                        document->Insert(start, edit.NewText);
                        ++applied;
                    }

                    void* openKey{};
                    for (auto const& [key, state] : m_documentTabs)
                    {
                        if (!state.Path.empty() && state.Path.lexically_normal() == path)
                        {
                            openKey = key;
                            break;
                        }
                    }
                    if (openKey)
                    {
                        SynchronizeDocumentFromModel(openKey, previous);
                    }
                    else
                    {
                        document->Save();
                    }
                }

                SetOutput(L"clangd", applied == 0
                          ? L"No rename edits were applied."
                          : L"Renamed symbol in " + std::to_wstring(editsByPath.size()) + L" file(s).");
            }
        }
    }

    void MainView::RequestCompletionForDocument(void* key)
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }

        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const offset = static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart()));
        auto const boundedOffset = (std::min)(offset, text.size());
        auto const line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(boundedOffset), L'\n'));
        auto const lineStart = boundedOffset == 0 ? std::wstring::npos : text.rfind(L'\n', boundedOffset - 1);
        auto const column = boundedOffset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        auto completionStart = boundedOffset;
        while (completionStart > (lineStart == std::wstring::npos ? 0 : lineStart + 1))
        {
            auto const character = text[completionStart - 1];
            if (!(std::iswalnum(character) || character == L'_'))
            {
                break;
            }
            --completionStart;
        }
        m_completionDocumentKey = key;
        m_completionStart = completionStart;
        m_completionLength = boundedOffset - completionStart;
        m_languageService.RequestCompletion(ToFileUri(found->second.Path), line, column);
    }

    void MainView::RequestAutomaticCompletion(void* key)
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            return;
        }

        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())), text.size());
        if (offset == 0)
        {
            return;
        }

        bool trigger = text[offset - 1] == L'.' || text[offset - 1] == L'>';
        if (text[offset - 1] == L':' && offset > 1 && text[offset - 2] == L':')
        {
            trigger = true;
        }
        if (trigger)
        {
            RequestCompletionForDocument(key);
        }
    }

    void MainView::RequestHoverForActiveDocument()
    {
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(active));
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }
        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())),
            text.size());
        auto const line = static_cast<std::size_t>(std::count(
            text.begin(),
            text.begin() + static_cast<std::ptrdiff_t>(offset),
            L'\n'));
        auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
        auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        m_pendingHoverRequestId = m_languageService.RequestHover(ToFileUri(found->second.Path), line, column);
        SetOutput(L"clangd", L"Hover requested.");
    }

    void MainView::RequestDefinitionForActiveDocument()
    {
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(active));
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }
        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())),
            text.size());
        auto const line = static_cast<std::size_t>(std::count(
            text.begin(),
            text.begin() + static_cast<std::ptrdiff_t>(offset),
            L'\n'));
        auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
        auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        m_pendingDefinitionRequestId = m_languageService.RequestDefinition(ToFileUri(found->second.Path), line, column);
        SetOutput(L"clangd", L"Go to definition requested.");
    }

    void MainView::RequestReferencesForActiveDocument()
    {
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(active));
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }
        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())), text.size());
        auto const line = static_cast<std::size_t>(std::count(
            text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), L'\n'));
        auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
        auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        m_pendingReferencesRequestId = m_languageService.RequestReferences(
            ToFileUri(found->second.Path), line, column);
        SetOutput(L"clangd", L"Find all references requested.");
    }

    void MainView::RequestSemanticTokensForDocument(void* key)
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            return;
        }
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }
        m_pendingSemanticTokenRequests.emplace(
            m_languageService.RequestSemanticTokens(ToFileUri(found->second.Path)), key);
    }

    void MainView::RequestDocumentSymbolsForActiveDocument()
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const key = winrt::get_abi(active);
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }
        m_pendingDocumentSymbolsDocumentKey = key;
        m_pendingDocumentSymbolsRequestId = m_languageService.RequestDocumentSymbols(ToFileUri(found->second.Path));
        SetOutput(L"clangd", L"Document outline requested.");
    }

    void MainView::RequestFormattingForActiveDocument()
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const key = winrt::get_abi(active);
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }
        m_pendingFormattingDocumentKey = key;
        m_pendingFormattingRequestId = m_languageService.RequestFormatting(ToFileUri(found->second.Path));
        SetOutput(L"clangd", L"Formatting document requested.");
    }

    void MainView::RequestCodeActionsForActiveDocument()
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }

        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(active));
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const cursor = (std::min)(static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())), text.size());
        auto const line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(cursor), L'\n'));
        auto const lineStart = cursor == 0 ? std::wstring::npos : text.rfind(L'\n', cursor - 1);
        auto const column = cursor - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        m_pendingCodeActionRequestId = m_languageService.RequestCodeActions(
            ToFileUri(found->second.Path), line, column, line, column);
        SetOutput(L"clangd", L"Quick actions requested.");
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ShowCodeActionsAsync(
        std::vector<::VisualForge::EditorCore::LSP::LspCodeAction> actions)
    {
        if (actions.empty() || !XamlRoot())
        {
            co_return;
        }

        ListView list;
        list.SelectionMode(ListViewSelectionMode::Single);
        for (auto const& action : actions)
        {
            list.Items().Append(box_value(hstring{ action.Title }));
        }
        list.SelectedIndex(0);
        list.MaxHeight(360);

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Quick Actions"));
        dialog.Content(list);
        dialog.PrimaryButtonText(L"Apply");
        dialog.SecondaryButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const selectedIndex = list.SelectedIndex();
        if (selectedIndex < 0 || static_cast<std::size_t>(selectedIndex) >= actions.size())
        {
            co_return;
        }

        auto const& selected = actions[static_cast<std::size_t>(selectedIndex)];
        std::unordered_map<std::wstring, std::vector<::VisualForge::EditorCore::LSP::LspTextEdit>> editsByPath;
        for (auto const& edit : selected.TextEdits)
        {
            auto path = PathFromFileUri(edit.Location.Uri).lexically_normal();
            if (!path.empty() && std::filesystem::exists(path))
            {
                editsByPath[path.wstring()].push_back(edit);
            }
        }

        if (editsByPath.empty())
        {
            SetOutput(L"clangd", L"The selected quick action did not contain applicable edits.");
            co_return;
        }

        auto offsetFromPosition = [](std::wstring_view text, std::size_t line, std::size_t column)
            {
                std::size_t currentLine{};
                std::size_t lineStart{};
                while (currentLine < line && lineStart < text.size())
                {
                    auto const newline = text.find(L'\n', lineStart);
                    if (newline == std::wstring_view::npos)
                    {
                        lineStart = text.size();
                        break;
                    }
                    lineStart = newline + 1;
                    ++currentLine;
                }
                auto const lineEnd = text.find(L'\n', lineStart);
                return (std::min)(lineStart + column, lineEnd == std::wstring_view::npos ? text.size() : lineEnd);
            };

        std::size_t applied{};
        for (auto& [pathText, edits] : editsByPath)
        {
            auto const path = std::filesystem::path{ pathText };
            auto* document = m_documentManager.Find(path);
            if (!document)
            {
                document = &m_documentManager.Open(path);
            }
            auto const previous = std::wstring{ document->Buffer().CreateSnapshot().Text() };
            std::sort(edits.begin(), edits.end(), [&](auto const& left, auto const& right)
                      {
                          return offsetFromPosition(previous, left.Location.Line, left.Location.Column)
                > offsetFromPosition(previous, right.Location.Line, right.Location.Column);
                      });
            for (auto const& edit : edits)
            {
                auto const start = offsetFromPosition(previous, edit.Location.Line, edit.Location.Column);
                auto const end = offsetFromPosition(previous, edit.Location.EndLine, edit.Location.EndColumn);
                if (start > end || end > previous.size())
                {
                    continue;
                }
                document->Delete(start, end - start);
                document->Insert(start, edit.NewText);
                ++applied;
            }

            void* openKey{};
            for (auto const& [key, state] : m_documentTabs)
            {
                if (!state.Path.empty() && state.Path.lexically_normal() == path)
                {
                    openKey = key;
                    break;
                }
            }
            if (openKey)
            {
                SynchronizeDocumentFromModel(openKey, previous);
            }
            else
            {
                document->Save();
            }
        }

        SetOutput(L"clangd", applied == 0
                  ? L"No quick action edits were applied."
                  : L"Applied quick action: " + selected.Title);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RenameActiveSymbolAsync()
    {
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            co_return;
        }

        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            co_return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(active));
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            co_return;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const cursor = (std::min)(static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())), text.size());
        auto start = cursor;
        auto end = cursor;
        while (start > 0 && (std::iswalnum(text[start - 1]) || text[start - 1] == L'_')) --start;
        while (end < text.size() && (std::iswalnum(text[end]) || text[end] == L'_')) ++end;
        if (start == end)
        {
            SetOutput(L"clangd", L"Place the caret on a symbol before renaming.");
            co_return;
        }

        TextBox nameBox;
        nameBox.Text(hstring{ text.substr(start, end - start) });
        nameBox.SelectAll();
        nameBox.PlaceholderText(L"New symbol name");
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Rename Symbol"));
        dialog.Content(nameBox);
        dialog.PrimaryButtonText(L"Rename");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const newName = std::wstring{ nameBox.Text() };
        if (newName.empty() || !(std::iswalpha(newName.front()) || newName.front() == L'_')
            || !std::all_of(newName.begin() + 1, newName.end(), [](wchar_t character)
                            {
                                return std::iswalnum(character) || character == L'_';
                            }))
        {
            SetOutput(L"clangd", L"The new symbol name is not a valid identifier.");
            co_return;
        }

        std::size_t const line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(cursor), L'\n'));
        auto const lineStart = cursor == 0 ? std::wstring::npos : text.rfind(L'\n', cursor - 1);
        auto const column = cursor - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        m_pendingRenameRequestId = m_languageService.RequestRename(ToFileUri(found->second.Path), line, column, newName);
        SetOutput(L"clangd", L"Rename requested.");
    }

    void MainView::HoverKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }
        RequestHoverForActiveDocument();
    }

    void MainView::GoToDefinitionKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }
        RequestDefinitionForActiveDocument();
    }

    void MainView::FindReferencesKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        if (m_languageService.State() != ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            SetOutput(L"clangd", L"Language service is not running.");
            return;
        }
        RequestReferencesForActiveDocument();
    }

    void MainView::RenameKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        RenameActiveSymbolAsync();
    }

    void MainView::FormatDocumentKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        RequestFormattingForActiveDocument();
    }

    void MainView::CodeActionsKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        RequestCodeActionsForActiveDocument();
    }

    void MainView::ToggleBreakpointKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(active));
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            return;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart())),
            text.size());
        auto const line = static_cast<int>(std::count(
            text.begin(),
            text.begin() + static_cast<std::ptrdiff_t>(offset),
            L'\n')) + 1;
        ToggleBreakpointForDocument(winrt::get_abi(active), line);
    }

    void MainView::ToggleBreakpointForDocument(void* key, int32_t line)
    {
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end() || found->second.Path.empty() || line <= 0)
        {
            return;
        }

        auto const breakpointKey = found->second.Path.wstring();
        auto& lines = m_breakpoints[breakpointKey];
        auto const existing = std::find(lines.begin(), lines.end(), line);
        auto const enabled = existing == lines.end();
        if (enabled)
        {
            lines.push_back(line);
            SetOutput(L"Debug", L"Breakpoint added at line " + std::to_wstring(line) + L".");
        }
        else
        {
            lines.erase(existing);
            SetOutput(L"Debug", L"Breakpoint removed from line " + std::to_wstring(line) + L".");
        }
        found->second.Editor.SetBreakpoint(line, enabled);
        if (m_debugAdapter.State() == ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            m_debugAdapter.SetBreakpoints(breakpointKey, lines);
        }
        SaveBreakpoints();
    }

    void MainView::CallStackList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            return;
        }
        auto selected = CallStackList().SelectedItem().try_as<ListViewItem>();
        if (!selected)
        {
            return;
        }
        auto const found = m_stackFrames.find(winrt::get_abi(selected));
        if (found == m_stackFrames.end() || found->second.SourcePath.empty())
        {
            return;
        }
        m_selectedFrameId = found->second.Id;
        LocalsTree().RootNodes().Clear();
        m_pendingVariableRequests.clear();
        m_debugAdapter.RequestScopes(m_selectedFrameId);
        OpenDiagnosticAsync(
            std::filesystem::path{ found->second.SourcePath },
            static_cast<std::size_t>((std::max)(1, found->second.Line)),
            static_cast<std::size_t>((std::max)(1, found->second.Column)));
    }

    void MainView::LocalsTree_Expanding(IInspectable const&, TreeViewExpandingEventArgs const& args)
    {
        if (m_debugAdapter.State() != ::VisualForge::EditorCore::DAP::DapClientState::Running)
        {
            return;
        }
        auto const node = args.Node();
        auto const found = m_variableReferences.find(winrt::get_abi(node));
        if (found == m_variableReferences.end() || m_loadedVariableNodes.contains(winrt::get_abi(node)))
        {
            return;
        }

        m_loadedVariableNodes.insert(winrt::get_abi(node));
        node.Children().Clear();
        m_pendingVariableRequests.push_back({ found->second, node });
        m_debugAdapter.RequestVariables(found->second);
    }

    void MainView::CompletionListView_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        // Selection only previews a candidate. Enter, Tab, double-click, or an explicit
        // commit action inserts it, so arrow-key navigation never mutates the document.
    }

    void MainView::CompletionListView_KeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        switch (args.Key())
        {
            case Windows::System::VirtualKey::Enter:
            case Windows::System::VirtualKey::Tab:
                ApplySelectedCompletion();
                args.Handled(true);
                break;
            case Windows::System::VirtualKey::Escape:
                CompletionPopup().IsOpen(false);
                if (auto active = EditorTabs().SelectedItem().try_as<TabViewItem>())
                {
                    if (auto const found = m_documentTabs.find(winrt::get_abi(active)); found != m_documentTabs.end())
                    {
                        found->second.Editor.FocusEditor();
                    }
                }
                args.Handled(true);
                break;
            default:
                break;
        }
    }

    void MainView::CompletionListView_DoubleTapped(IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
    {
        ApplySelectedCompletion();
        args.Handled(true);
    }

    void MainView::ApplySelectedCompletion()
    {
        auto selected = CompletionListView().SelectedItem().try_as<ListViewItem>();
        if (!selected)
        {
            return;
        }

        auto const found = m_completionEntries.find(winrt::get_abi(selected));
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (found == m_completionEntries.end() || !active)
        {
            return;
        }

        auto const document = m_documentTabs.find(winrt::get_abi(active));
        if (document != m_documentTabs.end())
        {
            if (m_completionDocumentKey == winrt::get_abi(active))
            {
                auto const textLength = static_cast<std::size_t>(document->second.Editor.Text().size());
                auto const start = (std::min)(m_completionStart, textLength);
                auto const length = (std::min)(m_completionLength, textLength - start);
                document->second.Editor.Select(static_cast<int32_t>(start), static_cast<int32_t>(length));
            }
            document->second.Editor.ReplaceSelection(hstring{ found->second.InsertText });
            document->second.Editor.FocusEditor();
        }
        CompletionPopup().IsOpen(false);
        m_completionDocumentKey = nullptr;
        m_completionStart = 0;
        m_completionLength = 0;
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenPathAsync(std::filesystem::path const& path)
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        try
        {
            auto file = co_await StorageFile::GetFileFromPathAsync(path.c_str());
            co_await OpenStorageFileAsync(file);
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Editor", L"Open failed: " + std::wstring{ error.message() });
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenStorageFileAsync(StorageFile const& file)
    {
        auto const path = std::filesystem::path{ file.Path().c_str() };
        m_selectedProjectPath.clear();
        for (auto const& [key, state] : m_documentTabs)
        {
            if (state.Path == path)
            {
                EditorTabs().SelectedItem(state.Tab);
                if (!m_isRestoringNavigation)
                {
                    RecordNavigationPath(path);
                }
                co_return;
            }
        }

        auto& document = m_documentManager.Open(path);
        auto const text = hstring{ document.Buffer().CreateSnapshot().Text() };
        if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            m_languageService.DidOpenFile(ToFileUri(path), LanguageIdForPath(path), std::wstring{ text });
        }
        m_isLoadingDocument = true;
        auto tab = TabViewItem{};
        auto editor = winrt::VisualForge::UI::Xaml::View::Control::EditorDocumentControl{ nullptr };
        if (!m_firstDocumentTabInUse)
        {
            tab = ActiveDocumentTab();
            editor = MainEditorControl();
            m_firstDocumentTabInUse = true;
        }
        else
        {
            tab.IsClosable(true);
            tab.Content(editor);
            EditorTabs().TabItems().Append(tab);
        }
        ConfigureDocumentTab(tab);

        auto const key = winrt::get_abi(tab);
        m_documentTabs.emplace(key, DocumentTabState{ file, tab, editor, path, path, std::wstring{ text } });
        if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
        {
            RequestSemanticTokensForDocument(key);
        }
        std::error_code timestampError;
        m_documentTabs.at(key).LastWriteTime = std::filesystem::last_write_time(path, timestampError);
        auto const weakThis = get_weak();
        editor.TextChanged([weakThis, key](IInspectable const&, IInspectable const&)
                           {
                               if (auto strongThis = weakThis.get(); strongThis && !strongThis->m_isLoadingDocument)
                               {
                                   strongThis->SynchronizeDocument(key);
                                   strongThis->RequestAutomaticCompletion(key);
                               }
                           });
        editor.SelectionChanged([weakThis](IInspectable const&, IInspectable const&)
                                {
                                    if (auto strongThis = weakThis.get())
                                    {
                                        strongThis->UpdateEditorPosition();
                                    }
                                });
        editor.CompletionRequested([weakThis, key](IInspectable const&, IInspectable const&)
                                   {
                                       if (auto strongThis = weakThis.get())
                                       {
                                           strongThis->RequestCompletionForDocument(key);
                                       }
                                   });
        editor.BreakpointRequested([weakThis, key](IInspectable const&, int32_t line)
                                   {
                                       if (auto strongThis = weakThis.get())
                                       {
                                           strongThis->ToggleBreakpointForDocument(key, line);
                                       }
                                   });
        editor.SetText(text);
        if (auto const breakpoints = m_breakpoints.find(path.wstring()); breakpoints != m_breakpoints.end())
        {
            for (auto const line : breakpoints->second)
            {
                editor.SetBreakpoint(line, true);
            }
        }
        m_isLoadingDocument = false;
        ReloadEditorFromDocument(key);
        UpdateDocumentHeader(key);
        EditorTabs().SelectedItem(tab);
        if (!m_isRestoringNavigation)
        {
            RecordNavigationPath(path);
        }
        UpdateEditorPosition();
        SetOutput(L"Editor", L"Opened " + path.wstring());
        ShellStatusText().Text(L"Ready");
    }

    winrt::Windows::Foundation::IAsyncAction MainView::SaveFileAsync()
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected || !m_documentTabs.contains(winrt::get_abi(selected)))
        {
            SetOutput(L"Editor", L"Nothing to save. Open a file first.");
            co_return;
        }

        co_await SaveDocumentAsync(selected);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::SaveAllDocumentsAsync()
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        std::vector<TabViewItem> dirtyTabs;
        for (auto const& [_, state] : m_documentTabs)
        {
            if (state.IsDirty)
            {
                dirtyTabs.push_back(state.Tab);
            }
        }

        for (auto const& tab : dirtyTabs)
        {
            co_await SaveDocumentAsync(tab);
        }

        if (dirtyTabs.empty())
        {
            SetOutput(L"Editor", L"All open files are already saved.");
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::SaveDocumentAsync(TabViewItem const& tab)
    {
        auto const key = winrt::get_abi(tab);
        auto const weakThis = get_weak();
        auto const strongThis = weakThis.get();
        if (!strongThis)
        {
            co_return;
        }
        auto found = m_documentTabs.find(key);
        if (found == m_documentTabs.end())
        {
            co_return;
        }

        if (found->second.Path.empty())
        {
            co_await SaveDocumentAsAsync(tab);
            co_return;
        }

        try
        {
            std::error_code timestampError;
            auto const diskTimestamp = std::filesystem::last_write_time(found->second.Path, timestampError);
            if (!timestampError && diskTimestamp != found->second.LastWriteTime)
            {
                ContentDialog dialog;
                dialog.XamlRoot(XamlRoot());
                dialog.Title(box_value(L"File changed on disk"));
                dialog.Content(box_value(L"This file was changed outside VisualForge. Overwrite the newer disk version?"));
                dialog.PrimaryButtonText(L"Overwrite");
                dialog.SecondaryButtonText(L"Reload");
                dialog.CloseButtonText(L"Cancel");
                auto const result = co_await dialog.ShowAsync();
                auto resumedThis = weakThis.get();
                if (!resumedThis)
                {
                    co_return;
                }
                found = resumedThis->m_documentTabs.find(key);
                if (found == resumedThis->m_documentTabs.end())
                {
                    co_return;
                }
                if (result == ContentDialogResult::Secondary)
                {
                    resumedThis->ReloadExternalDocumentAsync(key);
                    co_return;
                }
                if (result != ContentDialogResult::Primary)
                {
                    co_return;
                }
            }
            SynchronizeDocument(winrt::get_abi(tab));
            if (auto document = m_documentManager.Find(found->second.DocumentKey))
            {
                document->Save();
            }
            found->second.IsDirty = false;
            std::error_code savedTimestampError;
            found->second.LastWriteTime = std::filesystem::last_write_time(found->second.Path, savedTimestampError);
            found->second.ExternalChangePending = false;
            UpdateDocumentHeader(winrt::get_abi(tab));
            SetOutput(L"Editor", L"Saved " + found->second.Path.wstring());
            ShellStatusText().Text(L"Saved");
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Editor", L"Save failed: " + std::wstring{ winrt::to_hstring(error.what()) });
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::SaveDocumentAsAsync(TabViewItem const& tab)
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        auto const found = m_documentTabs.find(winrt::get_abi(tab));
        if (found == m_documentTabs.end())
        {
            co_return;
        }

        try
        {
            FileSavePicker picker;
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            auto const suggestedName = found->second.Path.empty() ? std::wstring{ L"Untitled.cpp" } : found->second.Path.filename().wstring();
            picker.SuggestedFileName(hstring{ suggestedName });
            auto extensions = winrt::single_threaded_vector<hstring>();
            extensions.Append(L".cpp");
            extensions.Append(L".h");
            extensions.Append(L".xaml");
            extensions.Append(L".txt");
            picker.FileTypeChoices().Insert(L"Source files", extensions);
            auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHelper::GetWindowHandleFromWindow(winrt::VisualForge::implementation::App::window)));
            auto file = co_await picker.PickSaveFileAsync();
            if (!file)
            {
                co_return;
            }

            auto const path = std::filesystem::path{ file.Path().c_str() };
            for (auto const& [otherKey, other] : m_documentTabs)
            {
                if (otherKey != winrt::get_abi(tab) && other.Path == path)
                {
                    SetOutput(L"Editor", L"That file is already open.");
                    co_return;
                }
            }

            auto const oldPath = found->second.Path;
            SynchronizeDocument(winrt::get_abi(tab));
            if (!m_documentManager.SaveAs(found->second.DocumentKey, path))
            {
                SetOutput(L"Editor", L"Save As failed: document is no longer available.");
                co_return;
            }

            found->second.DocumentKey = path;
            found->second.Path = path;
            found->second.File = file;
            found->second.IsDirty = false;
            std::error_code timestampError;
            found->second.LastWriteTime = std::filesystem::last_write_time(path, timestampError);
            found->second.ExternalChangePending = false;
            if (!oldPath.empty() && oldPath != path)
            {
                if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
                {
                    m_languageService.DidCloseFile(ToFileUri(oldPath));
                    m_languageService.DidOpenFile(
                        ToFileUri(path),
                        LanguageIdForPath(path),
                        found->second.LastText);
                }

                if (auto breakpoints = m_breakpoints.find(oldPath.wstring()); breakpoints != m_breakpoints.end())
                {
                    auto lines = std::move(breakpoints->second);
                    m_breakpoints.erase(breakpoints);
                    m_breakpoints[path.wstring()] = std::move(lines);
                }
                if (auto diagnostics = m_diagnosticsByPath.find(oldPath.wstring()); diagnostics != m_diagnosticsByPath.end())
                {
                    auto entries = std::move(diagnostics->second);
                    m_diagnosticsByPath.erase(diagnostics);
                    for (auto& entry : entries)
                    {
                        entry.Path = path;
                    }
                    m_diagnosticsByPath[path.wstring()] = std::move(entries);
                }
                for (auto& [_, entry] : m_diagnosticEntries)
                {
                    if (entry.Path == oldPath)
                    {
                        entry.Path = path;
                    }
                }
                if (m_debugAdapter.State() == ::VisualForge::EditorCore::DAP::DapClientState::Running)
                {
                    m_debugAdapter.SetBreakpoints(oldPath.wstring(), {});
                    if (auto const breakpoints = m_breakpoints.find(path.wstring()); breakpoints != m_breakpoints.end())
                    {
                        m_debugAdapter.SetBreakpoints(path.wstring(), breakpoints->second);
                    }
                }
            }
            UpdateDocumentHeader(winrt::get_abi(tab));
            if (!m_workspaceRoot.empty())
            {
                PopulateWorkspaceTree();
                RefreshGitChangesAsync();
            }
            SetOutput(L"Editor", L"Saved " + path.wstring());
            ShellStatusText().Text(L"Saved");
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Editor", L"Save As failed: " + std::wstring{ error.message() });
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Editor", L"Save As failed: " + std::wstring{ winrt::to_hstring(error.what()) });
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RenameActiveFileAsync()
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            SetOutput(L"Editor", L"Select a document before renaming it.");
            co_return;
        }

        auto const tabKey = winrt::get_abi(selected);
        auto found = m_documentTabs.find(tabKey);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            SetOutput(L"Editor", L"Save the untitled document before renaming it.");
            co_return;
        }

        try
        {
            auto const oldPath = found->second.Path;
            TextBox nameBox;
            nameBox.Text(hstring{ oldPath.filename().wstring() });
            ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(box_value(L"Rename file"));
            dialog.Content(nameBox);
            dialog.PrimaryButtonText(L"Rename");
            dialog.CloseButtonText(L"Cancel");
            if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
            {
                co_return;
            }

            auto newName = std::wstring{ nameBox.Text() };
            if (newName.empty())
            {
                SetOutput(L"Editor", L"The file name cannot be empty.");
                co_return;
            }
            for (auto const character : newName)
            {
                if (!(std::iswalnum(character) || character == L'_' || character == L'-' || character == L'.'))
                {
                    SetOutput(L"Editor", L"Use a single file name without directory separators.");
                    co_return;
                }
            }
            if (newName.find(L'.') == std::wstring::npos)
            {
                newName += oldPath.extension().wstring();
            }
            auto const newPath = oldPath.parent_path() / newName;
            if (newPath == oldPath)
            {
                co_return;
            }
            if (std::filesystem::exists(newPath))
            {
                SetOutput(L"Editor", L"A file with that name already exists.");
                co_return;
            }

            if (found->second.IsDirty)
            {
                co_await SaveDocumentAsync(selected);
                found = m_documentTabs.find(tabKey);
                if (found == m_documentTabs.end() || found->second.IsDirty)
                {
                    SetOutput(L"Editor", L"Rename cancelled because the document could not be saved.");
                    co_return;
                }
            }

            SynchronizeDocument(tabKey);
            if (!m_documentManager.SaveAs(oldPath, newPath))
            {
                SetOutput(L"Editor", L"Rename failed: the document path could not be migrated.");
                co_return;
            }
            std::error_code removeError;
            std::filesystem::remove(oldPath, removeError);
            if (removeError)
            {
                SetOutput(L"Editor", L"The new file was created, but the old file could not be removed.");
            }

            auto newFile = co_await StorageFile::GetFileFromPathAsync(newPath.wstring());
            found = m_documentTabs.find(tabKey);
            if (found == m_documentTabs.end())
            {
                co_return;
            }
            found->second.DocumentKey = newPath;
            found->second.Path = newPath;
            found->second.File = newFile;
            found->second.IsDirty = false;
            found->second.ExternalChangePending = false;
            std::error_code timestampError;
            found->second.LastWriteTime = std::filesystem::last_write_time(newPath, timestampError);

            if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
            {
                m_languageService.DidCloseFile(ToFileUri(oldPath));
                m_languageService.DidOpenFile(ToFileUri(newPath), LanguageIdForPath(newPath), found->second.LastText);
            }
            if (auto breakpoints = m_breakpoints.find(oldPath.wstring()); breakpoints != m_breakpoints.end())
            {
                auto lines = std::move(breakpoints->second);
                m_breakpoints.erase(breakpoints);
                m_breakpoints[newPath.wstring()] = std::move(lines);
            }
            if (auto diagnostics = m_diagnosticsByPath.find(oldPath.wstring()); diagnostics != m_diagnosticsByPath.end())
            {
                auto entries = std::move(diagnostics->second);
                m_diagnosticsByPath.erase(diagnostics);
                for (auto& entry : entries)
                {
                    entry.Path = newPath;
                }
                m_diagnosticsByPath[newPath.wstring()] = std::move(entries);
            }
            for (auto& [_, entry] : m_diagnosticEntries)
            {
                if (entry.Path == oldPath)
                {
                    entry.Path = newPath;
                }
            }
            if (m_debugAdapter.State() == ::VisualForge::EditorCore::DAP::DapClientState::Running)
            {
                m_debugAdapter.SetBreakpoints(oldPath.wstring(), {});
                if (auto breakpoints = m_breakpoints.find(newPath.wstring()); breakpoints != m_breakpoints.end())
                {
                    m_debugAdapter.SetBreakpoints(newPath.wstring(), breakpoints->second);
                }
            }

            try
            {
                auto projectPath = FindOwningProject(oldPath);
                if (projectPath.empty())
                {
                    projectPath = m_startupProjectPath.empty() ? FindWorkspaceFile(L".vcxproj") : m_startupProjectPath;
                }
                if (!projectPath.empty() && std::filesystem::exists(projectPath))
                {
                    std::wstring projectError;
                    if (!::VisualForge::Integration::ProjectWorkspaceService::RenameItem(projectPath, oldPath, newPath, projectError))
                    {
                        SetOutput(L"Project", projectError);
                    }
                }
            }
            catch (std::exception const& error)
            {
                SetOutput(L"Project", std::wstring{ L"Project file was not updated: " } + winrt::to_hstring(error.what()).c_str());
            }

            UpdateDocumentHeader(tabKey);
            PopulateWorkspaceTree();
            RefreshGitChangesAsync();
            SetOutput(L"Editor", L"Renamed " + oldPath.filename().wstring() + L" to " + newPath.filename().wstring());
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Editor", L"Rename failed: " + std::wstring{ error.message() });
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Editor", std::wstring{ L"Rename failed: " } + winrt::to_hstring(error.what()).c_str());
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::DeleteActiveFileAsync()
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            SetOutput(L"Editor", L"Select a document before deleting it.");
            co_return;
        }

        auto const tabKey = winrt::get_abi(selected);
        auto found = m_documentTabs.find(tabKey);
        if (found == m_documentTabs.end() || found->second.Path.empty())
        {
            SetOutput(L"Editor", L"Only saved files can be deleted from the workspace.");
            co_return;
        }

        auto const path = found->second.Path;
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Delete file"));
        dialog.Content(box_value(hstring{ L"Delete " + path.filename().wstring() + L" from disk and the project?" }));
        dialog.PrimaryButtonText(L"Delete");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        try
        {
            if (found->second.IsDirty)
            {
                co_await SaveDocumentAsync(selected);
                found = m_documentTabs.find(tabKey);
                if (found == m_documentTabs.end() || found->second.IsDirty)
                {
                    SetOutput(L"Editor", L"Delete cancelled because the document could not be saved.");
                    co_return;
                }
            }

            co_await CloseDocumentAsync(selected);
            if (m_documentTabs.contains(tabKey))
            {
                co_return;
            }

            std::error_code removeError;
            std::filesystem::remove(path, removeError);
            if (removeError)
            {
                SetOutput(L"Editor", L"Delete failed: " + path.wstring());
                co_return;
            }
            m_breakpoints.erase(path.wstring());
            m_diagnosticsByPath.erase(path.wstring());
            m_diagnosticEntries.clear();
            DiagnosticsList().Items().Clear();
            if (m_debugAdapter.State() == ::VisualForge::EditorCore::DAP::DapClientState::Running)
            {
                m_debugAdapter.SetBreakpoints(path.wstring(), {});
            }

            try
            {
                auto projectPath = FindOwningProject(path);
                if (projectPath.empty())
                {
                    projectPath = m_startupProjectPath.empty() ? FindWorkspaceFile(L".vcxproj") : m_startupProjectPath;
                }
                if (!projectPath.empty() && std::filesystem::exists(projectPath))
                {
                    std::wstring projectError;
                    if (!::VisualForge::Integration::ProjectWorkspaceService::RemoveItem(projectPath, path, projectError))
                    {
                        SetOutput(L"Project", projectError);
                    }
                }
            }
            catch (std::exception const& error)
            {
                SetOutput(L"Project", std::wstring{ L"Project file was not updated: " } + winrt::to_hstring(error.what()).c_str());
            }

            PopulateWorkspaceTree();
            RefreshGitChangesAsync();
            SetOutput(L"Editor", L"Deleted " + path.wstring());
        }
        catch (winrt::hresult_error const& error)
        {
            SetOutput(L"Editor", L"Delete failed: " + std::wstring{ error.message() });
        }
        catch (std::exception const& error)
        {
            SetOutput(L"Editor", std::wstring{ L"Delete failed: " } + winrt::to_hstring(error.what()).c_str());
        }
    }

    void MainView::EditorTabs_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto strongthis = this->get_strong();
        if (!m_isPageInitialized)
        {
            return;
        }

        if (!m_selectedProjectPath.empty() && std::filesystem::exists(m_selectedProjectPath))
        {
            PropertyNameValue().Text(hstring{ m_selectedProjectPath.stem().wstring() });
            PropertyKindValue().Text(L"C++ Project");
            PropertyPathValue().Text(hstring{ m_selectedProjectPath.wstring() });
            PropertyStateValue().Text(m_selectedProjectPath == m_startupProjectPath
                                      ? L"Startup project" : L"Available project");
            SetStartupProjectButton().Visibility(m_selectedProjectPath == m_startupProjectPath
                                                 ? Visibility::Collapsed : Visibility::Visible);
            if (auto const item = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>())
            {
                PropertyConfigurationValue().Text(unbox_value<hstring>(item.Content()));
            }
            if (auto const item = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>())
            {
                PropertyPlatformValue().Text(unbox_value<hstring>(item.Content()));
            }
            return;
        }
        SetStartupProjectButton().Visibility(Visibility::Collapsed);

        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        if (auto const found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
        {
            m_selectedProjectPath.clear();
            m_activeFile = found->second.File;
            if (!m_isRestoringNavigation && !found->second.Path.empty())
            {
                RecordNavigationPath(found->second.Path);
            }
            found->second.Editor.FocusEditor();
            RefreshOpenDocumentsPanel();
            RefreshSearchMatches(true);
            UpdateEditorPosition();
            RequestDocumentSymbolsForActiveDocument();
        }
    }

    void MainView::EditorTabs_TabCloseRequested(TabView const&, TabViewTabCloseRequestedEventArgs const& args)
    {
        SaveEditorSession();
        CloseDocumentAsync(args.Tab());
    }

    void MainView::SaveKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        SaveFileAsync();
    }

    void MainView::NewFileKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        NewFileAsync();
    }

    void MainView::NewProjectItemKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        NewProjectItemAsync();
    }

    void MainView::SaveAsKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (selected)
        {
            SaveDocumentAsAsync(selected);
        }
    }

    void MainView::Undo_Click(IInspectable const&, RoutedEventArgs const&)
    {
        UndoActiveDocument();
    }

    void MainView::Redo_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RedoActiveDocument();
    }

    void MainView::Cut_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (selected)
        {
            if (auto found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
            {
                found->second.Editor.CutSelection();
            }
        }
    }

    void MainView::Copy_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (selected)
        {
            if (auto found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
            {
                found->second.Editor.CopySelection();
            }
        }
    }

    void MainView::Paste_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (selected)
        {
            if (auto found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
            {
                found->second.Editor.PasteClipboardAsync();
            }
        }
    }

    void MainView::SelectAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (selected)
        {
            if (auto found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
            {
                found->second.Editor.SelectAllText();
            }
        }
    }

    void MainView::RenameActiveSymbol_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RenameActiveSymbolAsync();
    }

    void MainView::FormatDocument_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RequestFormattingForActiveDocument();
    }

    void MainView::CodeActions_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RequestCodeActionsForActiveDocument();
    }

    void MainView::GoToLine_Click(IInspectable const&, RoutedEventArgs const&)
    {
        GoToLineAsync();
    }

    void MainView::UndoKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        UndoActiveDocument();
    }

    void MainView::RedoKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        RedoActiveDocument();
    }

    winrt::Windows::Foundation::IAsyncAction MainView::CloseDocumentAsync(TabViewItem const& tab)
    {
        auto const weakThis = get_weak();
        auto const found = m_documentTabs.find(winrt::get_abi(tab));
        if (found == m_documentTabs.end())
        {
            co_return;
        }

        if (!found->second.IsDirty)
        {
            CloseDocument(tab);
            co_return;
        }

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Unsaved changes"));
        dialog.Content(box_value(L"Save changes before closing this document?"));
        dialog.PrimaryButtonText(L"Save");
        dialog.SecondaryButtonText(L"Discard");
        dialog.CloseButtonText(L"Cancel");
        auto const result = co_await dialog.ShowAsync();
        auto strongThis = weakThis.get();
        if (!strongThis)
        {
            co_return;
        }
        if (result == ContentDialogResult::Primary)
        {
            co_await strongThis->SaveDocumentAsync(tab);
            strongThis = weakThis.get();
            if (strongThis)
            {
                strongThis->CloseDocument(tab);
            }
        }
        else if (result == ContentDialogResult::Secondary)
        {
            strongThis->CloseDocument(tab);
        }
    }

    void MainView::ConfigureDocumentTab(TabViewItem const& tab)
    {
        if (!tab || !tab.IsClosable())
        {
            return;
        }

        MenuFlyout flyout;
        auto addAction = [&](std::wstring const& text, int action)
            {
                auto item = MenuFlyoutItem{};
                item.Text(hstring{ text });
                item.Click([weakThis = get_weak(), tab, action](IInspectable const&, RoutedEventArgs const&)
                           {
                               if (auto strongThis = weakThis.get())
                               {
                                   switch (action)
                                   {
                                       case 0:
                                           strongThis->CloseDocumentAsync(tab);
                                           break;
                                       case 1:
                                           strongThis->CloseOtherDocumentsAsync(tab);
                                           break;
                                       case 2:
                                           strongThis->CloseAllDocumentsAsync();
                                           break;
                                       default:
                                           break;
                                   }
                               }
                           });
                flyout.Items().Append(item);
            };

        addAction(L"Close", 0);
        addAction(L"Close Others", 1);
        addAction(L"Close All Documents", 2);
        tab.ContextFlyout(flyout);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::CloseOtherDocumentsAsync(TabViewItem const& keepTab)
    {
        std::vector<TabViewItem> tabs;
        for (uint32_t index = 0; index < EditorTabs().TabItems().Size(); ++index)
        {
            auto tab = EditorTabs().TabItems().GetAt(index).try_as<TabViewItem>();
            if (tab && tab != keepTab && m_documentTabs.contains(winrt::get_abi(tab)))
            {
                tabs.push_back(tab);
            }
        }

        for (auto const& tab : tabs)
        {
            if (m_documentTabs.contains(winrt::get_abi(tab)))
            {
                co_await CloseDocumentAsync(tab);
            }
        }
        if (keepTab && m_documentTabs.contains(winrt::get_abi(keepTab)))
        {
            EditorTabs().SelectedItem(keepTab);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::CloseAllDocumentsAsync()
    {
        std::vector<TabViewItem> tabs;
        for (uint32_t index = 0; index < EditorTabs().TabItems().Size(); ++index)
        {
            auto tab = EditorTabs().TabItems().GetAt(index).try_as<TabViewItem>();
            if (tab && m_documentTabs.contains(winrt::get_abi(tab)))
            {
                tabs.push_back(tab);
            }
        }

        for (auto const& tab : tabs)
        {
            if (m_documentTabs.contains(winrt::get_abi(tab)))
            {
                co_await CloseDocumentAsync(tab);
            }
        }
    }

    void MainView::CloseDocument(TabViewItem const& tab)
    {
        auto const key = winrt::get_abi(tab);
        auto const wasInitialTab = tab == ActiveDocumentTab();
        if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
        {
            if (!found->second.Path.empty()
                && m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
            {
                m_languageService.DidCloseFile(ToFileUri(found->second.Path));
            }
            (void)m_documentManager.Close(found->second.DocumentKey);
        }
        m_documentTabs.erase(key);
        if (wasInitialTab)
        {
            // Keep the first editor host in the TabView. New documents reuse this
            // host, so removing it would leave the next document without a tab.
            m_firstDocumentTabInUse = false;
            ActiveDocumentTab().Header(box_value(L"MainView.xaml"));
            ActiveDocumentTab().IsClosable(true);
            MainEditorControl().SetText(L"");
            EditorTabs().SelectedItem(ActiveDocumentTab());
        }
        else
        {
            uint32_t index{};
            if (EditorTabs().TabItems().IndexOf(tab, index))
            {
                EditorTabs().TabItems().RemoveAt(index);
            }
        }
        m_activeFile = nullptr;
        RefreshOpenDocumentsPanel();
        SetOutput(L"Editor", L"Closed document.");
    }

    void MainView::UpdateDocumentHeader(void* key)
    {
        if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
        {
            auto title = found->second.Path.empty() ? found->second.DocumentKey.filename().wstring() : found->second.Path.filename().wstring();
            if (title.rfind(L"untitled:", 0) == 0)
            {
                title = L"Untitled " + title.substr(9);
            }
            if (found->second.IsDirty)
            {
                title = L"* " + title;
            }
            found->second.Tab.Header(box_value(hstring{ title }));
        }
        RefreshOpenDocumentsPanel();
        UpdatePropertiesPanel();
    }

    void MainView::RefreshOpenDocumentsPanel()
    {
        if (!m_isPageInitialized || !OpenDocumentsList())
        {
            return;
        }

        m_isSynchronizingDocumentList = true;
        m_openDocumentItems.clear();
        OpenDocumentsList().Items().Clear();

        auto query = std::wstring{ OpenDocumentsSearchBox().Text() };
        std::transform(query.begin(), query.end(), query.begin(), [](wchar_t value)
                       {
                           return static_cast<wchar_t>(std::towlower(value));
                       });

        std::size_t visibleCount{};
        TabViewItem activeTab{ nullptr };
        if (auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>())
        {
            activeTab = selected;
        }

        for (uint32_t tabIndex = 0; tabIndex < EditorTabs().TabItems().Size(); ++tabIndex)
        {
            auto tab = EditorTabs().TabItems().GetAt(tabIndex).try_as<TabViewItem>();
            if (!tab)
            {
                continue;
            }
            auto const found = m_documentTabs.find(winrt::get_abi(tab));
            if (found == m_documentTabs.end())
            {
                continue;
            }
            auto const& state = found->second;
            auto title = state.Path.empty() ? state.DocumentKey.filename().wstring() : state.Path.filename().wstring();
            if (title.rfind(L"untitled:", 0) == 0)
            {
                title = L"Untitled " + title.substr(9);
            }
            if (state.IsDirty)
            {
                title = L"* " + title;
            }

            auto searchable = title;
            std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t value)
                           {
                               return static_cast<wchar_t>(std::towlower(value));
                           });
            if (!query.empty() && searchable.find(query) == std::wstring::npos)
            {
                continue;
            }

            auto item = ListViewItem{};
            item.Content(box_value(hstring{ title }));
            item.Tag(state.Tab);
            m_openDocumentItems.emplace(winrt::get_abi(item), state.Tab);
            OpenDocumentsList().Items().Append(item);
            if (activeTab && state.Tab == activeTab)
            {
                OpenDocumentsList().SelectedItem(item);
            }
            ++visibleCount;
        }

        OpenDocumentsSummaryText().Text(hstring{
            std::to_wstring(visibleCount) + (visibleCount == 1 ? L" open document" : L" open documents") });
        m_isSynchronizingDocumentList = false;
    }

    void MainView::MarkDocumentDirty(void* key)
    {
        if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end() && !found->second.IsDirty)
        {
            found->second.IsDirty = true;
            UpdateDocumentHeader(key);
        }
    }

    void MainView::SynchronizeDocument(void* key)
    {
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end())
        {
            return;
        }

        auto document = m_documentManager.Find(found->second.DocumentKey);
        if (!document)
        {
            return;
        }

        auto const current = std::wstring{ found->second.Editor.Text() };
        auto const& previous = found->second.LastText;
        if (current == previous)
        {
            return;
        }

        std::size_t prefix{};
        while (prefix < previous.size() && prefix < current.size() && previous[prefix] == current[prefix])
        {
            ++prefix;
        }

        std::size_t suffix{};
        while (suffix < previous.size() - prefix && suffix < current.size() - prefix
               && previous[previous.size() - 1 - suffix] == current[current.size() - 1 - suffix])
        {
            ++suffix;
        }

        auto const deletedLength = previous.size() - prefix - suffix;
        ShiftBreakpointsForEdit(
            found->second.Path,
            previous,
            prefix,
            deletedLength,
            std::wstring_view{ current }.substr(prefix, current.size() - prefix - suffix));
        if (deletedLength > 0)
        {
            document->Delete(prefix, deletedLength);
        }

        auto const insertedLength = current.size() - prefix - suffix;
        if (insertedLength > 0)
        {
            document->Insert(prefix, current.substr(prefix, insertedLength));
        }

        found->second.LastText = current;
        found->second.IsDirty = document->IsDirty();
        if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running
            && !found->second.Path.empty())
        {
            m_languageService.DidChange(ToFileUri(found->second.Path), current);
            RequestSemanticTokensForDocument(key);
        }
        UpdateDocumentHeader(key);
    }

    void MainView::ShiftBreakpointsForEdit(
        std::filesystem::path const& path,
        std::wstring_view previous,
        std::size_t start,
        std::size_t removedLength,
        std::wstring_view insertedText)
    {
        if (path.empty())
        {
            return;
        }

        auto const found = m_breakpoints.find(path.wstring());
        if (found == m_breakpoints.end() || found->second.empty() || start > previous.size())
        {
            return;
        }

        auto const end = (std::min)(previous.size(), start + removedLength);
        auto const lineAt = [previous](std::size_t offset)
            {
                return static_cast<int>(std::count(
                    previous.begin(),
                    previous.begin() + static_cast<std::ptrdiff_t>((std::min)(offset, previous.size())),
                    L'\n')) + 1;
            };
        auto const firstLine = lineAt(start);
        auto const lastLine = lineAt(end);
        auto const removedLines = static_cast<int>(std::count(
            previous.begin() + static_cast<std::ptrdiff_t>(start),
            previous.begin() + static_cast<std::ptrdiff_t>(end),
            L'\n'));
        auto const insertedLines = static_cast<int>(std::count(insertedText.begin(), insertedText.end(), L'\n'));
        auto const lineDelta = insertedLines - removedLines;
        auto const atLineStart = start == 0 || previous[start - 1] == L'\n';

        std::vector<int> shifted;
        shifted.reserve(found->second.size());
        for (auto line : found->second)
        {
            if (removedLength == 0)
            {
                if (line > firstLine || (atLineStart && line == firstLine && insertedLines > 0))
                {
                    line += lineDelta;
                }
            }
            else if (line > lastLine)
            {
                line += lineDelta;
            }
            else if (line >= firstLine)
            {
                line = firstLine;
            }

            if (line > 0)
            {
                shifted.push_back(line);
            }
        }
        std::sort(shifted.begin(), shifted.end());
        shifted.erase(std::unique(shifted.begin(), shifted.end()), shifted.end());
        found->second = std::move(shifted);
    }

    void MainView::ReloadEditorFromDocument(void* key)
    {
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end())
        {
            return;
        }

        auto document = m_documentManager.Find(found->second.DocumentKey);
        if (!document)
        {
            return;
        }

        auto const text = document->Buffer().CreateSnapshot().Text();
        m_isLoadingDocument = true;
        found->second.Editor.SetText(hstring{ text });
        m_isLoadingDocument = false;
        found->second.LastText = text;
        found->second.IsDirty = document->IsDirty();
        if (!found->second.Path.empty())
        {
            if (auto const breakpoints = m_breakpoints.find(found->second.Path.wstring()); breakpoints != m_breakpoints.end())
            {
                for (auto const line : breakpoints->second)
                {
                    found->second.Editor.SetBreakpoint(line, true);
                }
            }
            if (auto const diagnostics = m_diagnosticsByPath.find(found->second.Path.wstring());
                diagnostics != m_diagnosticsByPath.end())
            {
                for (auto const& diagnostic : diagnostics->second)
                {
                    found->second.Editor.SetDiagnosticLine(static_cast<int32_t>(diagnostic.Line), true);
                }
            }
        }
        UpdateDocumentHeader(key);
    }

    void MainView::SynchronizeDocumentFromModel(void* key, std::wstring_view previousText)
    {
        auto const found = m_documentTabs.find(key);
        if (found == m_documentTabs.end())
        {
            return;
        }

        auto document = m_documentManager.Find(found->second.DocumentKey);
        if (!document)
        {
            return;
        }

        std::wstring current{ document->Buffer().CreateSnapshot().Text() };
        if (current == previousText)
        {
            return;
        }

        std::size_t prefix{};
        while (prefix < previousText.size() && prefix < current.size()
               && previousText[prefix] == current[prefix])
        {
            ++prefix;
        }

        std::size_t suffix{};
        while (suffix < previousText.size() - prefix && suffix < current.size() - prefix
               && previousText[previousText.size() - 1 - suffix] == current[current.size() - 1 - suffix])
        {
            ++suffix;
        }

        auto const removedLength = previousText.size() - prefix - suffix;
        auto const insertedLength = current.size() - prefix - suffix;
        ShiftBreakpointsForEdit(
            found->second.Path,
            previousText,
            prefix,
            removedLength,
            std::wstring_view{ current }.substr(prefix, insertedLength));
        found->second.LastText = current;
        found->second.IsDirty = document->IsDirty();
        if (m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running
            && !found->second.Path.empty())
        {
            m_languageService.DidChange(ToFileUri(found->second.Path), current);
        }
        ReloadEditorFromDocument(key);
        if (auto updated = m_documentTabs.find(key); updated != m_documentTabs.end())
        {
            updated->second.Editor.Select(static_cast<int32_t>(prefix + insertedLength), 0);
        }
    }

    void MainView::UndoActiveDocument()
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        auto const key = winrt::get_abi(selected);
        if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
        {
            if (auto document = m_documentManager.Find(found->second.DocumentKey))
            {
                auto const previous = found->second.LastText;
                if (document->Undo())
                {
                    SynchronizeDocumentFromModel(key, previous);
                }
            }
        }
    }

    void MainView::RedoActiveDocument()
    {
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        auto const key = winrt::get_abi(selected);
        if (auto const found = m_documentTabs.find(key); found != m_documentTabs.end())
        {
            if (auto document = m_documentManager.Find(found->second.DocumentKey))
            {
                auto const previous = found->second.LastText;
                if (document->Redo())
                {
                    SynchronizeDocumentFromModel(key, previous);
                }
            }
        }
    }

    void MainView::FindTextBox_TextChanged(AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
    {
        RefreshSearchMatches(true);
    }

    void MainView::FindOptions_Changed(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshSearchMatches(true);
    }

    void MainView::FindNext_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NavigateSearchMatch(1);
    }

    void MainView::FindPrevious_Click(IInspectable const&, RoutedEventArgs const&)
    {
        NavigateSearchMatch(-1);
    }

    void MainView::Replace_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ReplaceCurrentMatch();
    }

    void MainView::ReplaceAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ReplaceAllMatches();
    }

    void MainView::FindInFiles_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SearchWorkspaceAsync();
    }

    void MainView::ReplaceInFiles_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ReplaceWorkspaceAsync();
    }

    void MainView::SearchResultsList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto selected = SearchResultsList().SelectedItem().try_as<ListViewItem>();
        if (selected)
        {
            if (auto const found = m_workspaceSearchResults.find(winrt::get_abi(selected)); found != m_workspaceSearchResults.end())
            {
                OpenSearchResultAsync(
                    found->second.Path,
                    found->second.Line,
                    found->second.Column,
                    std::wstring{ FindTextBox().Text() }.size());
            }
        }
    }

    void MainView::DiagnosticsList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto selected = DiagnosticsList().SelectedItem().try_as<ListViewItem>();
        if (selected)
        {
            if (auto const found = m_diagnosticEntries.find(winrt::get_abi(selected)); found != m_diagnosticEntries.end())
            {
                OpenDiagnosticAsync(found->second.Path, found->second.Line, found->second.Column);
            }
        }
    }

    void MainView::DocumentOutlineList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto selected = DocumentOutlineList().SelectedItem().try_as<ListViewItem>();
        if (!selected)
        {
            return;
        }
        auto const found = m_documentSymbolEntries.find(winrt::get_abi(selected));
        if (found == m_documentSymbolEntries.end())
        {
            return;
        }
        auto active = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!active)
        {
            return;
        }
        auto const document = m_documentTabs.find(winrt::get_abi(active));
        if (document == m_documentTabs.end())
        {
            return;
        }
        auto const text = std::wstring{ document->second.Editor.Text() };
        std::size_t offset{};
        for (std::size_t line = 0; line < found->second.Line && offset < text.size(); ++line)
        {
            auto const newline = text.find(L'\n', offset);
            offset = newline == std::wstring::npos ? text.size() : newline + 1;
        }
        offset = (std::min)(offset + found->second.Column, text.size());
        document->second.Editor.Select(static_cast<int32_t>(offset), 0);
        document->second.Editor.FocusEditor();
        UpdateEditorPosition();
    }

    void MainView::DiagnosticsSearchBox_TextChanged(
        AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
    {
        FilterDiagnosticsList();
    }

    void MainView::ErrorFilterButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_showErrorDiagnostics = !m_showErrorDiagnostics;
        FilterDiagnosticsList();
    }

    void MainView::WarningFilterButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_showWarningDiagnostics = !m_showWarningDiagnostics;
        FilterDiagnosticsList();
    }

    void MainView::MessageFilterButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_showMessageDiagnostics = !m_showMessageDiagnostics;
        FilterDiagnosticsList();
    }

    void MainView::FilterDiagnosticsList()
    {
        if (!DiagnosticsSearchBox())
        {
            return;
        }

        auto query = std::wstring{ DiagnosticsSearchBox().Text() };
        std::transform(query.begin(), query.end(), query.begin(), [](wchar_t value)
                       {
                           return static_cast<wchar_t>(std::towlower(value));
                       });

        for (uint32_t index = 0; index < DiagnosticsList().Items().Size(); ++index)
        {
            auto item = DiagnosticsList().Items().GetAt(index).try_as<ListViewItem>();
            if (!item)
            {
                continue;
            }

            auto const diagnostic = m_diagnosticEntries.find(winrt::get_abi(item));
            if (diagnostic == m_diagnosticEntries.end())
            {
                item.Visibility(Visibility::Collapsed);
                continue;
            }
            auto const severityVisible = diagnostic->second.Severity == L"error"
                ? m_showErrorDiagnostics
                : diagnostic->second.Severity == L"warning"
                ? m_showWarningDiagnostics
                : m_showMessageDiagnostics;
            auto text = std::wstring{ unbox_value_or<hstring>(item.Content(), L"") };
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value)
                           {
                               return static_cast<wchar_t>(std::towlower(value));
                           });
            item.Visibility(severityVisible && (query.empty() || text.find(query) != std::wstring::npos)
                            ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    void MainView::FindKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        FocusFindControl(false);
    }

    void MainView::ReplaceKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        FocusFindControl(true);
    }

    void MainView::GoToLineKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        GoToLineAsync();
    }

    void MainView::QuickOpenKeyboardAccelerator_Invoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&, Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        QuickOpenBox().Focus(FocusState::Programmatic);
        QuickOpenBox().Text(L"");
    }

    void MainView::CommandPaletteKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        ShowCommandPaletteAsync();
    }

    void MainView::QuickOpenBox_TextChanged(AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
    {
        QuickOpenBox().Items().Clear();
        m_quickOpenResults.clear();
        if (m_workspaceRoot.empty())
        {
            return;
        }

        auto query = std::wstring{ QuickOpenBox().Text() };
        std::transform(query.begin(), query.end(), query.begin(), [](wchar_t value)
                       {
                           return static_cast<wchar_t>(std::towlower(value));
                       });

        for (auto const& path : m_workspaceFiles)
        {
            if (m_quickOpenResults.size() >= 40)
            {
                break;
            }

            std::error_code error;
            auto const relative = std::filesystem::relative(path, m_workspaceRoot, error).wstring();
            auto searchable = relative;
            std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](wchar_t value)
                           {
                               return static_cast<wchar_t>(std::towlower(value));
                           });
            if (!query.empty() && searchable.find(query) == std::wstring::npos)
            {
                continue;
            }

            m_quickOpenResults.push_back(path);
            QuickOpenBox().Items().Append(box_value(hstring{ relative }));
        }
    }

    void MainView::QuickOpenBox_QuerySubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        auto displayName = unbox_value_or<hstring>(args.ChosenSuggestion(), QuickOpenBox().Text());
        if (auto path = FindQuickOpenPath(displayName); !path.empty())
        {
            OpenQuickOpenResultAsync(path);
        }
    }

    void MainView::OpenDocumentsSearchBox_TextChanged(
        AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&)
    {
        RefreshOpenDocumentsPanel();
    }

    void MainView::OpenDocumentsList_SelectionChanged(
        IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_isSynchronizingDocumentList)
        {
            return;
        }

        auto selected = OpenDocumentsList().SelectedItem().try_as<ListViewItem>();
        if (!selected)
        {
            return;
        }

        if (auto tab = selected.Tag().try_as<TabViewItem>())
        {
            EditorTabs().SelectedItem(tab);
            UpdateEditorPosition();
        }
    }

    void MainView::NextDocumentKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected || EditorTabs().TabItems().Size() < 2)
        {
            return;
        }

        uint32_t index{};
        if (EditorTabs().TabItems().IndexOf(selected, index))
        {
            auto const count = EditorTabs().TabItems().Size();
            for (uint32_t step = 1; step < count + 1; ++step)
            {
                auto const candidateIndex = (index + step) % count;
                auto candidate = EditorTabs().TabItems().GetAt(candidateIndex).try_as<TabViewItem>();
                if (candidate && candidate.Visibility() != Visibility::Collapsed)
                {
                    EditorTabs().SelectedIndex(static_cast<int32_t>(candidateIndex));
                    break;
                }
            }
        }
    }

    void MainView::PreviousDocumentKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected || EditorTabs().TabItems().Size() < 2)
        {
            return;
        }

        uint32_t index{};
        if (EditorTabs().TabItems().IndexOf(selected, index))
        {
            auto const count = EditorTabs().TabItems().Size();
            for (uint32_t step = 1; step < count + 1; ++step)
            {
                auto const candidateIndex = (index + count - step) % count;
                auto candidate = EditorTabs().TabItems().GetAt(candidateIndex).try_as<TabViewItem>();
                if (candidate && candidate.Visibility() != Visibility::Collapsed)
                {
                    EditorTabs().SelectedIndex(static_cast<int32_t>(candidateIndex));
                    break;
                }
            }
        }
    }

    void MainView::CloseDocumentKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        if (auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
            selected && m_documentTabs.contains(winrt::get_abi(selected)))
        {
            CloseDocumentAsync(selected);
        }
    }

    void MainView::RefreshSolutionExplorer_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Workspace", L"Open a workspace before refreshing Solution Explorer.");
            return;
        }

        PopulateWorkspaceTree();
        SetOutput(L"Workspace", L"Solution Explorer refreshed.");
    }

    void MainView::RefreshDocumentOutline_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RequestDocumentSymbolsForActiveDocument();
    }

    void MainView::SolutionExplorerProperties_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_isRightToolWindowHidden = false;
        RightToolWindowGrid().Visibility(Visibility::Visible);
        RightToolTabs().SelectedIndex(2);
        UpdatePropertiesPanel();
    }

    void MainView::SolutionExplorerShowAllFiles_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_showAllSolutionFiles = !m_showAllSolutionFiles;
        if (!m_solutionPath.empty())
        {
            Core::AppSettingsDatabase::Instance().SetBool(
                Core::AppSettingsDatabase::CAT_UI,
                "workspace.solutionExplorer.showAll." + winrt::to_string(hstring{ m_solutionPath.wstring() }),
                m_showAllSolutionFiles);
        }
        PopulateWorkspaceTree();
        SetOutput(L"Solution Explorer", m_showAllSolutionFiles ? L"Showing all files." : L"Showing project files.");
    }

    void MainView::SolutionExplorerSearchBox_TextChanged(
        AutoSuggestBox const& sender,
        AutoSuggestBoxTextChangedEventArgs const&)
    {
        m_solutionExplorerFilter = sender.Text().c_str();
        if (!m_solutionPath.empty())
        {
            Core::AppSettingsDatabase::Instance().SetStringW(
                Core::AppSettingsDatabase::CAT_UI,
                "workspace.solutionExplorer.filter." + winrt::to_string(hstring{ m_solutionPath.wstring() }),
                m_solutionExplorerFilter);
        }
        SolutionExplorer().SetFilter(sender.Text());
    }

    void MainView::SetStartupProject_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_selectedProjectPath.empty() || !std::filesystem::exists(m_selectedProjectPath))
        {
            return;
        }

        m_startupProjectPath = m_selectedProjectPath;
        Core::AppSettingsDatabase::Instance().SetStringW(
            Core::AppSettingsDatabase::CAT_UI,
            "startup.project",
            m_startupProjectPath.wstring());
        SolutionExplorer().SetStartupProject(hstring{ m_startupProjectPath.wstring() });
        LoadProjectConfigurations();
        SetOutput(L"Project", L"Startup project: " + m_startupProjectPath.stem().wstring());
        UpdatePropertiesPanel();
    }

    void MainView::RefreshGitChanges_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshGitChangesAsync();
    }

    void MainView::GitCommit_Click(IInspectable const&, RoutedEventArgs const&)
    {
        CommitGitChangesAsync();
    }

    void MainView::GitPull_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Git", L"Open a workspace before pulling changes.");
            return;
        }

        ::VisualForge::Integration::GitAdapter adapter;
        RunGitCommandAsync(adapter.CreatePullCommand(m_workspaceRoot), L"Git pull");
    }

    void MainView::GitPush_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Git", L"Open a workspace before pushing changes.");
            return;
        }

        ::VisualForge::Integration::GitAdapter adapter;
        RunGitCommandAsync(adapter.CreatePushCommand(m_workspaceRoot), L"Git push");
    }

    void MainView::ManageBranches_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ManageGitBranchesAsync();
    }

    void MainView::GitChangesList_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto selected = GitChangesList().SelectedItem().try_as<ListViewItem>();
        if (selected)
        {
            if (auto const found = m_gitChanges.find(winrt::get_abi(selected)); found != m_gitChanges.end())
            {
                LoadGitDiffAsync(found->second.Path);
            }
        }
    }

    void MainView::RefreshSearchMatches(bool resetSelection)
    {
        m_searchMatches.clear();
        m_searchMatchIndex = 0;
        FindSummaryText().Text(L"");

        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        auto const found = m_documentTabs.find(winrt::get_abi(selected));
        if (found == m_documentTabs.end())
        {
            return;
        }

        auto document = m_documentManager.Find(found->second.DocumentKey);
        auto const query = std::wstring{ FindTextBox().Text() };
        if (!document || query.empty())
        {
            return;
        }

        ::VisualForge::EditorCore::Search::SearchOptions options;
        auto const matchCase = MatchCaseToggle().IsChecked();
        auto const wholeWord = WholeWordToggle().IsChecked();
        options.MatchCase = matchCase ? matchCase.Value() : false;
        options.WholeWord = wholeWord ? wholeWord.Value() : false;
        m_searchMatches = ::VisualForge::EditorCore::Search::SearchEngine::FindAll(document->Buffer(), query, options);
        FindSummaryText().Text(hstring{ std::to_wstring(m_searchMatches.size()) + L" matches" });
        if (resetSelection && !m_searchMatches.empty())
        {
            NavigateSearchMatch(0);
        }
    }

    void MainView::NavigateSearchMatch(int direction)
    {
        if (m_searchMatches.empty())
        {
            return;
        }

        if (direction > 0)
        {
            m_searchMatchIndex = (m_searchMatchIndex + 1) % m_searchMatches.size();
        }
        else if (direction < 0)
        {
            m_searchMatchIndex = (m_searchMatchIndex + m_searchMatches.size() - 1) % m_searchMatches.size();
        }

        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        if (auto const found = m_documentTabs.find(winrt::get_abi(selected)); found != m_documentTabs.end())
        {
            auto const& match = m_searchMatches[m_searchMatchIndex];
            found->second.Editor.Select(static_cast<int32_t>(match.Start), static_cast<int32_t>(match.Length));
            found->second.Editor.FocusEditor();
            FindSummaryText().Text(hstring{ std::to_wstring(m_searchMatchIndex + 1) + L" / " + std::to_wstring(m_searchMatches.size()) });
        }
    }

    void MainView::ReplaceCurrentMatch()
    {
        if (m_searchMatches.empty())
        {
            return;
        }

        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        auto const found = m_documentTabs.find(winrt::get_abi(selected));
        if (found == m_documentTabs.end())
        {
            return;
        }

        auto document = m_documentManager.Find(found->second.DocumentKey);
        if (!document)
        {
            return;
        }

        auto const& match = m_searchMatches[m_searchMatchIndex];
        auto const replacement = std::wstring{ ReplaceTextBox().Text() };
        document->Delete(match.Start, match.Length);
        document->Insert(match.Start, replacement);
        ReloadEditorFromDocument(winrt::get_abi(selected));
        UpdateDocumentHeader(winrt::get_abi(selected));
        RefreshSearchMatches(true);
    }

    void MainView::ReplaceAllMatches()
    {
        if (m_searchMatches.empty())
        {
            return;
        }

        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            return;
        }

        auto const found = m_documentTabs.find(winrt::get_abi(selected));
        if (found == m_documentTabs.end())
        {
            return;
        }

        auto document = m_documentManager.Find(found->second.DocumentKey);
        if (!document)
        {
            return;
        }

        auto const replacement = std::wstring{ ReplaceTextBox().Text() };
        for (auto iterator = m_searchMatches.rbegin(); iterator != m_searchMatches.rend(); ++iterator)
        {
            document->Delete(iterator->Start, iterator->Length);
            document->Insert(iterator->Start, replacement);
        }

        auto const count = m_searchMatches.size();
        ReloadEditorFromDocument(winrt::get_abi(selected));
        UpdateDocumentHeader(winrt::get_abi(selected));
        SetOutput(L"Search", L"Replaced " + std::to_wstring(count) + L" matches.");
        RefreshSearchMatches(true);
    }

    void MainView::FocusFindControl(bool replacement)
    {
        auto control = replacement
            ? ReplaceTextBox().try_as<::winrt::Microsoft::UI::Xaml::Controls::Control>()
            : FindTextBox().try_as<::winrt::Microsoft::UI::Xaml::Controls::Control>();
        if (control)
        {
            control.Focus(FocusState::Programmatic);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::SearchWorkspaceAsync()
    {
        auto const query = std::wstring{ FindTextBox().Text() };
        if (query.empty())
        {
            SetOutput(L"Search", L"Enter text to search the workspace.");
            co_return;
        }

        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Search", L"Open a workspace before searching files.");
            co_return;
        }

        auto const root = m_workspaceRoot;
        auto const matchCase = MatchCaseToggle().IsChecked();
        auto const wholeWord = WholeWordToggle().IsChecked();
        auto const isMatchCase = matchCase ? matchCase.Value() : false;
        auto const isWholeWord = wholeWord ? wholeWord.Value() : false;
        auto const queue = DispatcherQueue();
        auto weakThis = get_weak();
        ShellStatusText().Text(L"Searching workspace...");
        co_await winrt::resume_background();

        std::vector<WorkspaceSearchResult> results;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator{ root, std::filesystem::directory_options::skip_permission_denied, error };
        std::filesystem::recursive_directory_iterator const end;
        for (; iterator != end && !error && results.size() < 200; iterator.increment(error))
        {
            auto const& entry = *iterator;
            if (entry.is_directory(error))
            {
                auto const name = entry.path().filename().wstring();
                if (name == L".git" || name == L".vs" || name == L"obj" || name == L"x64" || name == L"vcpkg_installed")
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }

            if (!entry.is_regular_file(error))
            {
                continue;
            }

            auto const extension = entry.path().extension().wstring();
            if (extension != L".cpp" && extension != L".h" && extension != L".ixx" && extension != L".xaml"
                && extension != L".json" && extension != L".md" && extension != L".props" && extension != L".targets"
                && extension != L".vcxproj" && extension != L".slnx")
            {
                continue;
            }

            std::wstring contents;
            try
            {
                contents = ::VisualForge::EditorCore::Document::DocumentManager::ReadTextFile(entry.path());
            }
            catch (std::exception const&)
            {
                continue;
            }

            auto searchable = contents;
            auto searchableQuery = query;
            if (!isMatchCase)
            {
                auto lower = [](wchar_t value)
                    {
                        return static_cast<wchar_t>(std::towlower(value));
                    };
                std::transform(searchable.begin(), searchable.end(), searchable.begin(), lower);
                std::transform(searchableQuery.begin(), searchableQuery.end(), searchableQuery.begin(), lower);
            }

            std::size_t offset{};
            while (results.size() < 200)
            {
                auto const match = searchable.find(searchableQuery, offset);
                if (match == std::string::npos)
                {
                    break;
                }
                if (isWholeWord)
                {
                    auto const isWord = [](wchar_t value)
                        {
                            return std::iswalnum(value) != 0 || value == L'_';
                        };
                    auto const beforeIsWord = match > 0 && isWord(contents[match - 1]);
                    auto const after = match + query.size();
                    auto const afterIsWord = after < contents.size() && isWord(contents[after]);
                    if (beforeIsWord || afterIsWord)
                    {
                        offset = match + (std::max<std::size_t>)(1, query.size());
                        continue;
                    }
                }

                auto const lineStart = contents.rfind(L'\n', match);
                auto const previewStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
                auto const lineEnd = contents.find(L'\n', match);
                auto preview = contents.substr(previewStart, (lineEnd == std::wstring::npos ? contents.size() : lineEnd) - previewStart);
                std::replace(preview.begin(), preview.end(), L'\t', L' ');
                results.push_back({
                    entry.path(),
                    static_cast<std::size_t>(std::count(contents.begin(), contents.begin() + static_cast<std::ptrdiff_t>(match), L'\n')) + 1,
                    match - previewStart + 1,
                    preview });
                offset = match + (std::max<std::size_t>)(1, query.size());
            }
        }

        (void)queue.TryEnqueue([weakThis, results = std::move(results)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }
                                   strongThis->SearchResultsList().Items().Clear();
                                   strongThis->m_workspaceSearchResults.clear();
                                   for (auto const& result : results)
                                   {
                                       auto item = ListViewItem{};
                                       auto const label = result.Path.filename().wstring() + L":" + std::to_wstring(result.Line) + L"  " + result.Preview;
                                       item.Content(box_value(hstring{ label }));
                                       strongThis->SearchResultsList().Items().Append(item);
                                       strongThis->m_workspaceSearchResults.emplace(winrt::get_abi(item), result);
                                   }

                                   strongThis->FindSummaryText().Text(hstring{ std::to_wstring(results.size()) + L" workspace matches" });
                                   strongThis->ShellStatusText().Text(results.empty() ? L"No workspace matches" : L"Workspace search complete");
                               });
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ReplaceWorkspaceAsync()
    {
        auto const query = std::wstring{ FindTextBox().Text() };
        auto const replacement = std::wstring{ ReplaceTextBox().Text() };
        if (query.empty())
        {
            SetOutput(L"Search", L"Enter text to replace in the workspace.");
            co_return;
        }
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Search", L"Open a workspace before replacing files.");
            co_return;
        }

        ContentDialog confirmation;
        confirmation.XamlRoot(XamlRoot());
        confirmation.Title(box_value(L"Replace in Files"));
        confirmation.Content(box_value(hstring{
            L"Replace all matches of \"" + query + L"\" in the workspace files?" }));
        confirmation.PrimaryButtonText(L"Replace All");
        confirmation.CloseButtonText(L"Cancel");
        if (co_await confirmation.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const root = m_workspaceRoot;
        auto const matchCase = MatchCaseToggle().IsChecked();
        auto const wholeWord = WholeWordToggle().IsChecked();
        ::VisualForge::EditorCore::Search::SearchOptions options{
            matchCase ? matchCase.Value() : false,
            wholeWord ? wholeWord.Value() : false };
        std::unordered_set<std::wstring> openPaths;
        for (auto const& [_, state] : m_documentTabs)
        {
            if (!state.Path.empty())
            {
                openPaths.insert(state.Path.wstring());
            }
        }

        ShellStatusText().Text(L"Replacing in workspace...");
        auto const queue = DispatcherQueue();
        auto weakThis = get_weak();
        co_await winrt::resume_background();

        std::size_t files{};
        std::size_t matches{};
        std::size_t failed{};
        std::vector<std::filesystem::path> openFiles;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator{
            root, std::filesystem::directory_options::skip_permission_denied, error };
        std::filesystem::recursive_directory_iterator const end;
        auto isSearchable = [](std::filesystem::path const& path)
            {
                auto const extension = path.extension().wstring();
                return extension == L".cpp" || extension == L".h" || extension == L".ixx" || extension == L".xaml"
                    || extension == L".json" || extension == L".md" || extension == L".props" || extension == L".targets"
                    || extension == L".vcxproj" || extension == L".slnx";
            };
        ::VisualForge::EditorCore::Document::DocumentManager workerDocuments;
        for (; iterator != end && !error; iterator.increment(error))
        {
            auto const& entry = *iterator;
            if (entry.is_directory(error))
            {
                auto const name = entry.path().filename().wstring();
                if (name == L".git" || name == L".vs" || name == L"obj" || name == L"x64" || name == L"vcpkg_installed")
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (!entry.is_regular_file(error) || !isSearchable(entry.path()))
            {
                continue;
            }

            if (openPaths.contains(entry.path().wstring()))
            {
                openFiles.push_back(entry.path());
                continue;
            }

            try
            {
                auto& document = workerDocuments.Open(entry.path());
                auto const count = ::VisualForge::EditorCore::Search::SearchEngine::ReplaceAll(
                    document.Buffer(), query, replacement, options);
                if (count > 0)
                {
                    document.Save();
                    ++files;
                    matches += count;
                }
            }
            catch (std::exception const&)
            {
                ++failed;
            }
        }

        (void)queue.TryEnqueue([weakThis, openFiles = std::move(openFiles), query = std::move(query), replacement = std::move(replacement), options, files, matches, failed]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }

                                   std::size_t openFilesChanged{};
                                   std::size_t openMatches{};
                                   std::size_t skippedDirty{};
                                   for (auto const& path : openFiles)
                                   {
                                       for (auto& [key, state] : strongThis->m_documentTabs)
                                       {
                                           if (state.Path != path)
                                           {
                                               continue;
                                           }
                                           if (state.IsDirty)
                                           {
                                               ++skippedDirty;
                                               break;
                                           }
                                           auto document = strongThis->m_documentManager.Find(state.DocumentKey);
                                           if (!document)
                                           {
                                               break;
                                           }
                                           auto const count = ::VisualForge::EditorCore::Search::SearchEngine::ReplaceAll(
                                               document->Buffer(), query, replacement, options);
                                           if (count > 0)
                                           {
                                               document->Save();
                                               strongThis->ReloadEditorFromDocument(key);
                                               if (strongThis->m_languageService.State() == ::VisualForge::EditorCore::LSP::LspClientState::Running)
                                               {
                                                   strongThis->m_languageService.DidChange(
                                                       ToFileUri(path), std::wstring{ document->Buffer().CreateSnapshot().Text() });
                                               }
                                               ++openFilesChanged;
                                               openMatches += count;
                                           }
                                           break;
                                       }
                                   }

                                   strongThis->SetOutput(L"Search",
                                                         L"Replaced " + std::to_wstring(matches + openMatches) + L" matches in "
                                                         + std::to_wstring(files + openFilesChanged) + L" files."
                                                         + (skippedDirty == 0 ? L"" : L" Skipped " + std::to_wstring(skippedDirty) + L" dirty files.")
                                                         + (failed == 0 ? L"" : L" Failed files: " + std::to_wstring(failed) + L"."));
                                   strongThis->ShellStatusText().Text(L"Workspace replace complete");
                                   strongThis->SearchWorkspaceAsync();
                               });
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenSearchResultAsync(std::filesystem::path const& path, std::size_t line, std::size_t column, std::size_t length)
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        co_await OpenPathAsync(path);
        for (auto const& [key, state] : m_documentTabs)
        {
            if (state.Path == path)
            {
                EditorTabs().SelectedItem(state.Tab);
                auto const text = std::wstring{ state.Editor.Text() };
                std::size_t offset{};
                for (std::size_t currentLine = 1; currentLine < line && offset < text.size(); ++currentLine)
                {
                    auto const newline = text.find(L'\n', offset);
                    if (newline == std::wstring::npos)
                    {
                        offset = text.size();
                        break;
                    }
                    offset = newline + 1;
                }
                if (column > 0)
                {
                    offset = (std::min)(offset + column - 1, text.size());
                }
                state.Editor.Select(static_cast<int32_t>(offset), static_cast<int32_t>(length));
                break;
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenQuickOpenResultAsync(std::filesystem::path const& path)
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        co_await OpenPathAsync(path);
        QuickOpenBox().Text(L"");
        QuickOpenBox().Focus(FocusState::Programmatic);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RefreshGitChangesAsync()
    {
        if (m_workspaceRoot.empty())
        {
            GitChangesList().Items().Clear();
            m_gitChanges.clear();
            GitChangeSummaryText().Text(L"No repository loaded");
            GitBranchStatusText().Text(L"git: unavailable");
            co_return;
        }

        auto const root = m_workspaceRoot;
        auto const queue = DispatcherQueue();
        auto weakThis = get_weak();
        GitChangeSummaryText().Text(L"Loading...");
        co_await winrt::resume_background();

        ::VisualForge::Integration::GitAdapter adapter;
        auto command = adapter.CreateStatusCommand(root);
        ::VisualForge::Tool::ProcessSession process;
        auto const started = process.Start(command);
        if (started)
        {
            (void)process.WaitForExit();
        }
        auto snapshot = process.Snapshot();
        auto output = snapshot.StdOut + snapshot.StdErr;
        if (output.size() > 16000)
        {
            output.erase(0, output.size() - 16000);
        }

        (void)queue.TryEnqueue([weakThis, started, exitCode = snapshot.ExitCode, output = std::move(output)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }
                                   if (!started)
                                   {
                                       strongThis->ParseGitStatus({});
                                       strongThis->GitChangeSummaryText().Text(L"Git unavailable");
                                       strongThis->GitBranchStatusText().Text(L"git: unavailable");
                                       strongThis->GitDiffOutputTextBox().Text(L"Git could not be started.");
                                       return;
                                   }

                                   strongThis->ParseGitStatus(output);
                                   if (exitCode != 0)
                                   {
                                       strongThis->GitChangeSummaryText().Text(L"Git command failed");
                                   }
                                   strongThis->ShellStatusText().Text(exitCode == 0 ? L"Git refreshed" : L"Git error");
                               });
    }

    winrt::Windows::Foundation::IAsyncAction MainView::LoadGitDiffAsync(std::filesystem::path const& path)
    {
        if (m_workspaceRoot.empty())
        {
            co_return;
        }

        auto const root = m_workspaceRoot;
        auto const queue = DispatcherQueue();
        auto weakThis = get_weak();
        GitDiffOutputTextBox().Text(L"Loading diff...");
        co_await winrt::resume_background();

        std::error_code relativeError;
        auto relativePath = std::filesystem::relative(path, root, relativeError);
        if (relativeError)
        {
            relativePath = path.filename();
        }
        ::VisualForge::Integration::GitAdapter adapter;
        auto command = adapter.CreateDiffCommand(root, relativePath);
        ::VisualForge::Tool::ProcessSession process;
        auto const started = process.Start(command);
        if (started)
        {
            (void)process.WaitForExit();
        }
        auto snapshot = process.Snapshot();
        auto output = snapshot.StdOut + snapshot.StdErr;
        if (output.size() > 30000)
        {
            output.erase(0, output.size() - 30000);
        }
        (void)queue.TryEnqueue([weakThis, started, output = std::move(output)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }
                                   if (!started)
                                   {
                                       strongThis->GitDiffOutputTextBox().Text(L"Git could not be started.");
                                       return;
                                   }

                                   auto const wideOutput = std::wstring{ winrt::to_hstring(output) };
                                   if (wideOutput.empty())
                                   {
                                       strongThis->GitDiffOutputTextBox().Text(L"No unstaged diff for this file.");
                                   }
                                   else
                                   {
                                       strongThis->GitDiffOutputTextBox().Text(hstring{ wideOutput });
                                   }
                               });
    }

    winrt::Windows::Foundation::IAsyncAction MainView::CommitGitChangesAsync()
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Git", L"Open a workspace before committing changes.");
            co_return;
        }

        TextBox messageBox;
        messageBox.AcceptsReturn(true);
        messageBox.Height(80);
        messageBox.PlaceholderText(L"Commit message");
        messageBox.TextWrapping(TextWrapping::Wrap);

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Commit Changes"));
        dialog.Content(messageBox);
        dialog.PrimaryButtonText(L"Commit");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto message = std::wstring{ messageBox.Text() };
        message.erase(message.begin(), std::find_if(message.begin(), message.end(), [](wchar_t value)
                                                    {
                                                        return !std::iswspace(value);
                                                    }));
        message.erase(std::find_if(message.rbegin(), message.rend(), [](wchar_t value)
                                   {
                                       return !std::iswspace(value);
                                   }).base(), message.end());
        if (message.empty())
        {
            SetOutput(L"Git", L"Commit message cannot be empty.");
            co_return;
        }

        ::VisualForge::Integration::GitAdapter adapter;
        co_await RunGitCommandAsync(adapter.CreateAddAllCommand(m_workspaceRoot), L"Git stage");
        co_await RunGitCommandAsync(adapter.CreateCommitCommand(m_workspaceRoot, std::move(message)), L"Git commit");
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ManageGitBranchesAsync()
    {
        if (m_workspaceRoot.empty())
        {
            SetOutput(L"Git", L"Open a workspace before managing branches.");
            co_return;
        }

        auto const root = m_workspaceRoot;
        auto const queue = DispatcherQueue();
        auto const weakThis = get_weak();
        co_await winrt::resume_background();

        ::VisualForge::Integration::GitAdapter adapter;
        ::VisualForge::Tool::ProcessSession process;
        auto const started = process.Start(adapter.CreateBranchListCommand(root));
        if (started)
        {
            (void)process.WaitForExit();
        }
        auto snapshot = process.Snapshot();
        auto output = snapshot.StdOut + snapshot.StdErr;
        (void)queue.TryEnqueue([weakThis, started, output = std::move(output)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }
                                   if (!started)
                                   {
                                       strongThis->SetOutput(L"Git", L"Git executable could not be started.");
                                       return;
                                   }

                                   std::vector<std::wstring> branches;
                                   std::istringstream lines{ output };
                                   std::string line;
                                   while (std::getline(lines, line))
                                   {
                                       while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
                                       {
                                           line.erase(line.begin());
                                       }
                                       if (!line.empty() && line.front() == '*')
                                       {
                                           line.erase(line.begin());
                                           while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
                                           {
                                               line.erase(line.begin());
                                           }
                                       }
                                       if (line.empty() || line.rfind("remotes/", 0) == 0 || line.find("->") != std::string::npos)
                                       {
                                           continue;
                                       }
                                       branches.push_back(std::wstring{ winrt::to_hstring(line) });
                                   }
                                   std::sort(branches.begin(), branches.end());
                                   branches.erase(std::unique(branches.begin(), branches.end()), branches.end());
                                   if (branches.empty())
                                   {
                                       strongThis->SetOutput(L"Git", L"No local branches found.");
                                       return;
                                   }
                                   strongThis->ShowGitBranchDialogAsync(std::move(branches));
                               });
    }

    winrt::Windows::Foundation::IAsyncAction MainView::ShowGitBranchDialogAsync(std::vector<std::wstring> branches)
    {
        ComboBox branchPicker;
        branchPicker.MinWidth(300);
        for (auto const& branch : branches)
        {
            branchPicker.Items().Append(box_value(hstring{ branch }));
        }
        branchPicker.SelectedIndex(0);

        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Manage Git Branches"));
        dialog.Content(branchPicker);
        dialog.PrimaryButtonText(L"Checkout");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const selected = branchPicker.SelectedItem().try_as<IInspectable>();
        if (!selected)
        {
            co_return;
        }
        auto const branch = unbox_value<hstring>(selected);
        ::VisualForge::Integration::GitAdapter adapter;
        co_await RunGitCommandAsync(
            adapter.CreateCheckoutCommand(m_workspaceRoot, std::wstring{ branch }),
            L"Git checkout");
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RunGitCommandAsync(
        ::VisualForge::Tool::ToolCommand command,
        std::wstring operation)
    {
        if (m_workspaceRoot.empty())
        {
            co_return;
        }

        auto const refreshGit = command.Kind == ::VisualForge::Tool::ToolKind::Git;
        auto const queue = DispatcherQueue();
        auto const weakThis = get_weak();
        co_await winrt::resume_background();

        ::VisualForge::Tool::ProcessSession process;
        auto const started = process.Start(command);
        if (started)
        {
            (void)process.WaitForExit();
        }
        auto snapshot = process.Snapshot();
        auto output = snapshot.StdOut + snapshot.StdErr;
        if (output.size() > 16000)
        {
            output.erase(0, output.size() - 16000);
        }

        (void)queue.TryEnqueue([
            weakThis,
            refreshGit,
            started,
            exitCode = snapshot.ExitCode,
            output = std::move(output),
            operation = std::move(operation)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }

                                   if (!started)
                                   {
                                       strongThis->SetOutput(operation, operation + L": executable could not be started.");
                                       strongThis->ShellStatusText().Text(operation + L" unavailable");
                                       return;
                                   }

                                   auto const wideOutput = std::wstring{ winrt::to_hstring(output) };
                                   auto const resultMessage = wideOutput.empty()
                                       ? (exitCode == 0 ? std::wstring{ L"Completed." } : operation + L" failed.")
                                       : wideOutput;
                                   strongThis->SetOutput(operation, resultMessage);
                                   strongThis->ShellStatusText().Text(exitCode == 0
                                                                      ? operation + L" completed" : operation + L" error");
                                   if (exitCode == 0 && refreshGit)
                                   {
                                       strongThis->RefreshGitChangesAsync();
                                   }
                               });
    }

    void MainView::ParseGitStatus(std::string const& output)
    {
        GitChangesList().Items().Clear();
        m_gitChanges.clear();
        std::string branch{ "unavailable" };
        std::size_t count{};
        std::istringstream lines{ output };
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.rfind("## ", 0) == 0)
            {
                branch = line.substr(3);
                continue;
            }
            if (line.size() < 4 || line[0] == '#')
            {
                continue;
            }

            auto const status = line.substr(0, 2);
            auto pathText = line.substr(3);
            if (pathText.empty())
            {
                continue;
            }
            if ((status[0] == 'R' || status[0] == 'C') && pathText.find(" -> ") != std::string::npos)
            {
                pathText = pathText.substr(pathText.rfind(" -> ") + 4);
            }

            auto const path = m_workspaceRoot / std::filesystem::path{ std::wstring{ winrt::to_hstring(pathText) } };
            auto item = ListViewItem{};
            auto const statusWide = std::wstring{ winrt::to_hstring(status) };
            auto const pathWide = std::wstring{ winrt::to_hstring(pathText) };
            item.Content(box_value(hstring{ statusWide + L"  " + pathWide }));
            GitChangesList().Items().Append(item);
            m_gitChanges.emplace(winrt::get_abi(item), GitChangeEntry{ path, status, pathWide });
            ++count;
        }

        GitBranchStatusText().Text(hstring{ L"git: " + std::wstring{ winrt::to_hstring(branch) } });
        GitChangeSummaryText().Text(hstring{ std::to_wstring(count) + L" changes" });
    }

    void MainView::LoadWorkspaceLayouts()
    {
        if (!m_viewModel)
        {
            return;
        }

        auto& workspace = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace();
        auto& database = Core::AppSettingsDatabase::Instance();
        auto& editing = workspace.Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing);
        auto& debugging = workspace.Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging);
        auto const savedConfiguration = database.GetString(Core::AppSettingsDatabase::CAT_UI, "build.configuration").value_or("Debug");
        auto const savedPlatform = database.GetString(Core::AppSettingsDatabase::CAT_UI, "build.platform").value_or("x64");
        for (uint32_t index = 0; index < ConfigurationComboBox().Items().Size(); ++index)
        {
            if (auto const item = ConfigurationComboBox().Items().GetAt(index).try_as<ComboBoxItem>();
                item && winrt::to_string(unbox_value<hstring>(item.Content())) == savedConfiguration)
            {
                ConfigurationComboBox().SelectedIndex(static_cast<int32_t>(index));
                break;
            }
        }
        for (uint32_t index = 0; index < PlatformComboBox().Items().Size(); ++index)
        {
            if (auto const item = PlatformComboBox().Items().GetAt(index).try_as<ComboBoxItem>();
                item && winrt::to_string(unbox_value<hstring>(item.Content())) == savedPlatform)
            {
                PlatformComboBox().SelectedIndex(static_cast<int32_t>(index));
                break;
            }
        }
        editing.BottomToolHeight = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_height", editing.BottomToolHeight)), 220, 520);
        debugging.BottomToolHeight = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_height", debugging.BottomToolHeight)), 260, 520);
        editing.RightToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.right_tab", editing.RightToolTabIndex)), 0, 4);
        debugging.RightToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.right_tab", debugging.RightToolTabIndex)), 0, 4);
        editing.BottomToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_tab", editing.BottomToolTabIndex)), 0, 4);
        debugging.BottomToolTabIndex = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_tab", debugging.BottomToolTabIndex)), 0, 4);
        editing.ShowDiagnosticTools = database.GetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.show_diagnostics").value_or(false);
        debugging.ShowDiagnosticTools = database.GetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.show_diagnostics").value_or(true);
        editing.LeftToolWidth = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.left_width", editing.LeftToolWidth)), 210, 520);
        debugging.LeftToolWidth = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.left_width", debugging.LeftToolWidth)), 210, 520);
        editing.RightToolWidth = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.right_width", editing.RightToolWidth)), 300, 620);
        debugging.RightToolWidth = (std::clamp)(static_cast<int>(database.GetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.right_width", debugging.RightToolWidth)), 300, 620);

        auto const savedMode = database.GetString(Core::AppSettingsDatabase::CAT_UI, "workspace.mode").value_or("editing");
        ApplyWorkspaceMode(savedMode == "debugging"
                           ? ::VisualForge::IDE::Shell::WorkspaceMode::Debugging
                           : ::VisualForge::IDE::Shell::WorkspaceMode::Editing, false);
    }

    void MainView::ApplyWorkspaceMode(::VisualForge::IDE::Shell::WorkspaceMode mode, bool persist)
    {
        if (!m_viewModel || !m_isPageInitialized)
        {
            return;
        }

        m_isApplyingWorkspaceMode = true;
        auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
        viewModel->Workspace().SetMode(mode);
        auto const& profile = viewModel->Workspace().Profile(mode);
        WorkspaceModeComboBox().SelectedIndex(mode == ::VisualForge::IDE::Shell::WorkspaceMode::Debugging ? 1 : 0);
        EditorAreaRow().Height({ 1, GridUnitType::Star });
        BottomToolRow().Height({ static_cast<double>(profile.BottomToolHeight), GridUnitType::Pixel });
        LeftToolColumn().Width({ static_cast<double>(profile.LeftToolWidth), GridUnitType::Pixel });
        RightToolColumn().Width({ static_cast<double>(profile.RightToolWidth), GridUnitType::Pixel });
        DebugToolsTab().Visibility(profile.ShowDiagnosticTools ? Visibility::Visible : Visibility::Collapsed);
        CallStackTab().Visibility(profile.ShowDiagnosticTools ? Visibility::Visible : Visibility::Collapsed);
        LocalsTab().Visibility(profile.ShowDiagnosticTools ? Visibility::Visible : Visibility::Collapsed);
        if (m_documentTabs.empty())
        {
            EditorTabs().SelectedItem(ActiveDocumentTab());
        }

        if (mode == ::VisualForge::IDE::Shell::WorkspaceMode::Debugging)
        {
            BottomToolWindowTitleText().Text(L"Debug Tools");
            RightToolTabs().SelectedIndex(profile.RightToolTabIndex);
            BottomToolTabs().SelectedIndex(profile.BottomToolTabIndex);
            DebugSessionText().Text(m_viewModel.DebugStatus());
            DebugEventsList().Items().Clear();
            auto event = ListViewItem{};
            event.Content(box_value(hstring{ L"Debug layout active. Press F5 to start the local target." }));
            DebugEventsList().Items().Append(event);
        }
        else
        {
            BottomToolWindowTitleText().Text(L"Error List - Current Project");
            RightToolTabs().SelectedIndex(profile.RightToolTabIndex);
            BottomToolTabs().SelectedIndex(profile.BottomToolTabIndex);
        }

        m_isApplyingWorkspaceMode = false;
        if (persist)
        {
            auto& database = Core::AppSettingsDatabase::Instance();
            database.SetString(Core::AppSettingsDatabase::CAT_UI, "workspace.mode", mode == ::VisualForge::IDE::Shell::WorkspaceMode::Debugging ? "debugging" : "editing");
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_height", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).BottomToolHeight);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_height", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).BottomToolHeight);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.right_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).RightToolTabIndex);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.right_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).RightToolTabIndex);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.bottom_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).BottomToolTabIndex);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.bottom_tab", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).BottomToolTabIndex);
            database.SetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.show_diagnostics", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).ShowDiagnosticTools);
            database.SetBool(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.show_diagnostics", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).ShowDiagnosticTools);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.left_width", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).LeftToolWidth);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.left_width", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).LeftToolWidth);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.editing.right_width", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Editing).RightToolWidth);
            database.SetInt(Core::AppSettingsDatabase::CAT_UI, "workspace.debugging.right_width", viewModel->Workspace().Profile(::VisualForge::IDE::Shell::WorkspaceMode::Debugging).RightToolWidth);
        }
    }

    void MainView::RightToolTabs_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!m_isPageInitialized || m_isApplyingWorkspaceMode || !m_viewModel)
        {
            return;
        }

        auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
        viewModel->Workspace().Profile(viewModel->Workspace().Mode()).RightToolTabIndex = RightToolTabs().SelectedIndex();
        ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
    }

    void MainView::BottomToolTabs_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (!m_isPageInitialized || m_isApplyingWorkspaceMode || !m_viewModel)
        {
            return;
        }

        auto* viewModel = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel);
        viewModel->Workspace().Profile(viewModel->Workspace().Mode()).BottomToolTabIndex = BottomToolTabs().SelectedIndex();
        ApplyWorkspaceMode(viewModel->Workspace().Mode(), true);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::GoToLineAsync()
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            co_return;
        }

        auto const found = m_documentTabs.find(winrt::get_abi(selected));
        if (found == m_documentTabs.end())
        {
            co_return;
        }

        TextBox input;
        input.Width(220);
        input.PlaceholderText(L"Line number");
        input.Text(L"1");
        input.SelectAll();

        ContentDialog dialog;
        dialog.Title(box_value(L"Go to Line"));
        dialog.Content(input);
        dialog.PrimaryButtonText(L"Go");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(this->XamlRoot());
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
        {
            co_return;
        }

        std::size_t requestedLine{};
        try
        {
            requestedLine = static_cast<std::size_t>(std::stoull(std::wstring{ input.Text() }));
        }
        catch (...)
        {
            SetOutput(L"Editor", L"Enter a valid line number.");
            co_return;
        }

        if (requestedLine == 0)
        {
            requestedLine = 1;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        std::size_t offset{};
        std::size_t lastLine = 1;
        for (auto position = text.find(L'\n'); position != std::wstring::npos; position = text.find(L'\n', position + 1))
        {
            ++lastLine;
        }
        requestedLine = (std::min)(requestedLine, lastLine);
        for (std::size_t currentLine = 1; currentLine < requestedLine; ++currentLine)
        {
            auto const newline = text.find(L'\n', offset);
            if (newline == std::wstring::npos)
            {
                offset = text.size();
                break;
            }
            offset = newline + 1;
        }

        found->second.Editor.Select(static_cast<int32_t>(offset), 0);
        found->second.Editor.FocusEditor();
        UpdateEditorPosition();
    }

    winrt::Windows::Foundation::IAsyncAction MainView::OpenDiagnosticAsync(std::filesystem::path const& path, std::size_t line, std::size_t column)
    {
        auto const strongThis = get_weak().get();
        if (!strongThis)
        {
            co_return;
        }
        co_await OpenPathAsync(path);
        for (auto const& [key, state] : m_documentTabs)
        {
            if (state.Path != path)
            {
                continue;
            }

            EditorTabs().SelectedItem(state.Tab);
            auto const text = std::wstring{ state.Editor.Text() };
            std::size_t offset{};
            for (std::size_t currentLine = 1; currentLine < line && offset < text.size(); ++currentLine)
            {
                auto const newline = text.find(L'\n', offset);
                if (newline == std::wstring::npos)
                {
                    offset = text.size();
                    break;
                }
                offset = newline + 1;
            }

            if (column > 0)
            {
                offset = (std::min)(offset + column - 1, text.size());
            }
            state.Editor.Select(static_cast<int32_t>(offset), 1);
            state.Editor.FocusEditor();
            break;
        }
    }

    void MainView::RebuildDiagnosticsList()
    {
        DiagnosticsList().Items().Clear();
        m_diagnosticEntries.clear();
        for (auto const& [_, state] : m_documentTabs)
        {
            state.Editor.ClearDiagnosticLines();
        }

        std::size_t errors{};
        std::size_t warnings{};
        std::size_t messages{};
        for (auto const& [__, diagnostics] : m_diagnosticsByPath)
        {
            for (auto const& entry : diagnostics)
            {
                if (entry.Severity == L"error")
                {
                    ++errors;
                }
                else if (entry.Severity == L"warning")
                {
                    ++warnings;
                }
                else
                {
                    ++messages;
                }

                for (auto const& [___, state] : m_documentTabs)
                {
                    if (state.Path == entry.Path)
                    {
                        state.Editor.SetDiagnosticLine(static_cast<int32_t>(entry.Line), true);
                        break;
                    }
                }

                auto item = ListViewItem{};
                item.Content(box_value(hstring{
                    entry.Severity + L" " + entry.Code + L"  " + entry.Path.filename().wstring()
                    + L":" + std::to_wstring(entry.Line) + L":" + std::to_wstring(entry.Column) + L"  " + entry.Message }));
                DiagnosticsList().Items().Append(item);
                m_diagnosticEntries.emplace(winrt::get_abi(item), entry);
            }
        }

        ProblemStatusText().Text(hstring{
            std::to_wstring(errors) + L" Errors, " + std::to_wstring(warnings) + L" Warnings" });
        ErrorCountText().Text(hstring{ std::to_wstring(errors) + L" Errors" });
        WarningCountText().Text(hstring{ std::to_wstring(warnings) + L" Warnings" });
        MessageCountText().Text(hstring{ std::to_wstring(messages) + L" Messages" });
        FilterDiagnosticsList();
    }

    void MainView::ParseBuildDiagnostics(std::wstring const& output)
    {
        DiagnosticsList().Items().Clear();
        m_diagnosticEntries.clear();
        for (auto it = m_diagnosticsByPath.begin(); it != m_diagnosticsByPath.end(); )
        {
            auto& diagnostics = it->second;
            diagnostics.erase(std::remove_if(diagnostics.begin(), diagnostics.end(), [](DiagnosticEntry const& entry)
                                             {
                                                 return entry.IsBuildDiagnostic;
                                             }), diagnostics.end());
            if (diagnostics.empty())
            {
                it = m_diagnosticsByPath.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto const& [_, state] : m_documentTabs)
        {
            state.Editor.ClearDiagnosticLines();
        }
        std::size_t errors{};
        std::size_t warnings{};
        std::size_t messages{};
        std::string narrow = winrt::to_string(hstring{ output });
        std::regex pattern{ R"((.+)\((\d+),(\d+)\):\s+(error|warning)\s+([A-Za-z]+\d+):\s*(.*))" };
        std::regex clangPattern{ R"((.+):(\d+):(\d+):\s+(fatal error|error|warning|note):\s*(.*))" };
        std::istringstream lines{ narrow };
        std::string line;
        while (std::getline(lines, line))
        {
            std::smatch match;
            bool const isMsvc = std::regex_match(line, match, pattern);
            bool const isClang = !isMsvc && std::regex_match(line, match, clangPattern);
            if (!isMsvc && !isClang)
            {
                continue;
            }

            auto path = std::filesystem::path{ winrt::to_hstring(match[1].str()).c_str() };
            if (path.is_relative())
            {
                path = m_workspaceRoot / path;
            }
            std::wstring severity;
            std::wstring code;
            std::wstring message;
            if (isClang)
            {
                severity = match[4].str() == "fatal error" ? L"error"
                    : match[4].str() == "warning" ? L"warning" : L"info";
                message = std::wstring{ winrt::to_hstring(match[5].str()) };
            }
            else
            {
                severity = std::wstring{ winrt::to_hstring(match[4].str()) };
                code = std::wstring{ winrt::to_hstring(match[5].str()) };
                message = std::wstring{ winrt::to_hstring(match[6].str()) };
            }
            DiagnosticEntry entry{
                path,
                static_cast<std::size_t>(std::stoull(match[2].str())),
                static_cast<std::size_t>(std::stoull(match[3].str())),
                std::move(severity),
                std::move(code),
                std::move(message),
                true };
            if (entry.Severity == L"error")
            {
                ++errors;
            }
            else if (entry.Severity == L"warning")
            {
                ++warnings;
            }
            else
            {
                ++messages;
            }

            auto item = ListViewItem{};
            auto const label = entry.Severity + L" " + entry.Code + L"  " + entry.Path.filename().wstring()
                + L":" + std::to_wstring(entry.Line) + L":" + std::to_wstring(entry.Column) + L"  " + entry.Message;
            item.Content(box_value(hstring{ label }));
            DiagnosticsList().Items().Append(item);
            m_diagnosticsByPath[entry.Path.wstring()].push_back(entry);
            for (auto const& [_, state] : m_documentTabs)
            {
                if (state.Path == entry.Path)
                {
                    state.Editor.SetDiagnosticLine(static_cast<int32_t>(entry.Line), true);
                    break;
                }
            }
            m_diagnosticEntries.emplace(winrt::get_abi(item), std::move(entry));
        }

        ErrorCountText().Text(hstring{ std::to_wstring(errors) + L" Errors" });
        WarningCountText().Text(hstring{ std::to_wstring(warnings) + L" Warnings" });
        MessageCountText().Text(hstring{ std::to_wstring(messages) + L" Messages" });
        RebuildDiagnosticsList();
        FilterDiagnosticsList();
    }

    void MainView::SetOutput(std::wstring const& source, std::wstring const& text)
    {
        if (!text.empty())
        {
            auto current = std::wstring{ OutputTextBox().Text() };
            if (!current.empty() && current.back() != L'\n')
            {
                current += L'\n';
            }
            current += L"[" + source + L"] " + text;
            if (current.size() > 30000)
            {
                current.erase(0, current.size() - 30000);
            }
            OutputTextBox().Text(hstring{ current });
        }
        if (m_viewModel)
        {
            winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Runtime().AppendOutput(source, text);
            RefreshStatusBar();
        }
    }

    void MainView::PopulateWorkspaceTree()
    {
        if (m_workspaceRoot.empty())
        {
            return;
        }

        SolutionExplorer().SetShowAllFiles(m_showAllSolutionFiles);
        SolutionExplorer().LoadWorkspace(
            hstring{ m_workspaceRoot.wstring() },
            hstring{ m_solutionPath.wstring() });
        SolutionExplorerSearchBox().Text(hstring{ m_solutionExplorerFilter });
        if (!m_solutionExplorerFilter.empty())
        {
            SolutionExplorer().SetFilter(hstring{ m_solutionExplorerFilter });
        }
        m_workspaceFiles.clear();
        auto addWorkspaceFile = [this](std::filesystem::path path)
        {
            path = path.lexically_normal();
            if (path.empty() || !std::filesystem::is_regular_file(path))
            {
                return;
            }
            std::error_code relativeError;
            auto const relative = std::filesystem::relative(path, m_workspaceRoot, relativeError);
            if (relativeError || relative.empty() || relative.native().find(L"..") == 0)
            {
                return;
            }
            if (std::find(m_workspaceFiles.begin(), m_workspaceFiles.end(), path) == m_workspaceFiles.end())
            {
                m_workspaceFiles.push_back(std::move(path));
            }
        };

        if (m_showAllSolutionFiles)
        {
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator{
                     m_workspaceRoot, std::filesystem::directory_options::skip_permission_denied, error };
                 iterator != std::filesystem::recursive_directory_iterator{} && !error;
                 iterator.increment(error))
            {
                if (iterator->is_regular_file(error))
                {
                    addWorkspaceFile(iterator->path());
                }
            }
            return;
        }

        // The normal solution view is project-backed. Read only the projects
        // declared by .slnx and their MSBuild item Includes; do not scan the repository.
        addWorkspaceFile(m_solutionPath);
        for (auto const& projectPath : ReadSolutionProjectPaths(m_solutionPath))
        {
            addWorkspaceFile(projectPath);
            try
            {
                std::ifstream stream{ projectPath, std::ios::binary };
                std::string xml{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
                Windows::Data::Xml::Dom::XmlDocument document;
                document.LoadXml(ReadXmlText(std::move(xml)));
                auto nodes = document.SelectNodes(
                    L"//*[local-name()='ClCompile' or local-name()='ClInclude' or local-name()='Page' "
                    L"or local-name()='None' or local-name()='ResourceCompile' or local-name()='Content' "
                    L"or local-name()='ProjectReference']");
                for (uint32_t index = 0; index < nodes.Length(); ++index)
                {
                    auto const includeAttribute = nodes.Item(index).Attributes().GetNamedItem(L"Include");
                    if (!includeAttribute || !includeAttribute.NodeValue())
                    {
                        continue;
                    }
                    auto const include = std::wstring{ unbox_value<hstring>(includeAttribute.NodeValue()) };
                    if (include.empty() || include.find(L"$(") != std::wstring::npos)
                    {
                        continue;
                    }
                    addWorkspaceFile(projectPath.parent_path() / std::filesystem::path{ include });
                }
            }
            catch (...)
            {
                SetOutput(L"Workspace", L"Could not read project items from " + projectPath.filename().wstring());
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainView::LoadProjectConfigurations()
    {
        if (m_solutionPath.empty())
        {
            co_return;
        }

        auto const solutionProjects = ReadSolutionProjectPaths(m_solutionPath);
        auto const projectPath = !m_startupProjectPath.empty()
            ? m_startupProjectPath
            : (solutionProjects.empty() ? std::filesystem::path{} : solutionProjects.front());
        if (projectPath.empty())
        {
            co_return;
        }

        std::vector<std::wstring> configurations;
        std::vector<std::wstring> platforms;
        try
        {
            std::vector<::VisualForge::Integration::ProjectConfigurationEntry> entries;
            co_await ::VisualForge::Integration::ProjectWorkspaceService::ReadConfigurations(projectPath, entries);
            for (auto const& entry : entries)
            {
                configurations.push_back(entry.Configuration);
                platforms.push_back(entry.Platform);
            }
        }
        catch (...)
        {
            co_return;
        }

        auto makeUnique = [](std::vector<std::wstring>& values)
            {
                std::sort(values.begin(), values.end());
                values.erase(std::unique(values.begin(), values.end()), values.end());
            };
        makeUnique(configurations);
        makeUnique(platforms);
        if (configurations.empty() || platforms.empty())
        {
            co_return;
        }

        auto const savedConfiguration = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI, "build.configuration").value_or(L"Debug");
        auto const savedPlatform = Core::AppSettingsDatabase::Instance().GetStringW(
            Core::AppSettingsDatabase::CAT_UI, "build.platform").value_or(L"x64");
        m_isLoadingProjectConfigurations = true;
        ConfigurationComboBox().Items().Clear();
        for (auto const& value : configurations)
        {
            auto item = ComboBoxItem{};
            item.Content(box_value(hstring{ value }));
            ConfigurationComboBox().Items().Append(item);
        }
        PlatformComboBox().Items().Clear();
        for (auto const& value : platforms)
        {
            auto item = ComboBoxItem{};
            item.Content(box_value(hstring{ value }));
            PlatformComboBox().Items().Append(item);
        }

        ConfigurationComboBox().SelectedIndex(0);
        PlatformComboBox().SelectedIndex(0);
        for (uint32_t index = 0; index < ConfigurationComboBox().Items().Size(); ++index)
        {
            auto item = ConfigurationComboBox().Items().GetAt(index).try_as<ComboBoxItem>();
            if (item && std::wstring{ unbox_value<hstring>(item.Content()) } == savedConfiguration)
            {
                ConfigurationComboBox().SelectedIndex(static_cast<int32_t>(index));
                break;
            }
        }
        for (uint32_t index = 0; index < PlatformComboBox().Items().Size(); ++index)
        {
            auto item = PlatformComboBox().Items().GetAt(index).try_as<ComboBoxItem>();
            if (item && std::wstring{ unbox_value<hstring>(item.Content()) } == savedPlatform)
            {
                PlatformComboBox().SelectedIndex(static_cast<int32_t>(index));
                break;
            }
        }
        m_isLoadingProjectConfigurations = false;
        UpdatePropertiesPanel();
    }

    std::filesystem::path MainView::FindQuickOpenPath(std::wstring_view displayName) const
    {
        for (auto const& path : m_quickOpenResults)
        {
            std::error_code error;
            auto const relative = std::filesystem::relative(path, m_workspaceRoot, error).wstring();
            if (relative == displayName || path.filename().wstring() == displayName)
            {
                return path;
            }
        }

        return {};
    }

    void MainView::UpdateEditorPosition()
    {
        if (!m_isPageInitialized)
        {
            return;
        }
        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            EditorPositionText().Text(L"Ln 1, Col 1");
            UpdatePropertiesPanel();
            return;
        }

        auto const found = m_documentTabs.find(winrt::get_abi(selected));
        if (found == m_documentTabs.end())
        {
            EditorPositionText().Text(L"Ln 1, Col 1");
            UpdatePropertiesPanel();
            return;
        }

        auto const text = std::wstring{ found->second.Editor.Text() };
        auto const selectionStart = static_cast<std::size_t>((std::max)(0, found->second.Editor.SelectionStart()));
        auto const offset = (std::min)(selectionStart, text.size());
        auto const lineStart = offset == 0 ? std::wstring::npos : text.rfind(L'\n', offset - 1);
        auto const line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), L'\n')) + 1;
        auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1) + 1;
        EditorPositionText().Text(hstring{ L"Ln " + std::to_wstring(line) + L", Col " + std::to_wstring(column) });
        UpdatePropertiesPanel();
    }

    void MainView::UpdatePropertiesPanel()
    {
        if (!m_isPageInitialized)
        {
            return;
        }

        auto selected = EditorTabs().SelectedItem().try_as<TabViewItem>();
        if (!selected)
        {
            PropertyNameValue().Text(L"No document");
            PropertyKindValue().Text(L"-");
            PropertyPathValue().Text(L"-");
            PropertyStateValue().Text(L"No document");
            return;
        }

        auto const found = m_documentTabs.find(winrt::get_abi(selected));
        if (found == m_documentTabs.end())
        {
            PropertyNameValue().Text(L"No document");
            PropertyKindValue().Text(L"-");
            PropertyPathValue().Text(L"-");
            PropertyStateValue().Text(L"No document");
            return;
        }

        auto const path = found->second.Path.empty() ? found->second.DocumentKey : found->second.Path;
        PropertyNameValue().Text(hstring{ path.filename().wstring() });
        PropertyPathValue().Text(hstring{ path.wstring() });
        PropertyKindValue().Text(hstring{ LanguageIdForPath(path) });
        PropertyStateValue().Text(found->second.IsDirty ? L"Modified" : L"Saved");
        if (auto const item = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>())
        {
            PropertyConfigurationValue().Text(unbox_value<hstring>(item.Content()));
        }
        if (auto const item = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>())
        {
            PropertyPlatformValue().Text(unbox_value<hstring>(item.Content()));
        }
    }

    std::filesystem::path MainView::FindWorkspaceFile(std::wstring_view extension) const
    {
        if (m_workspaceRoot.empty())
        {
            return {};
        }

        if (extension == L".slnx" && !m_solutionPath.empty() && std::filesystem::exists(m_solutionPath))
        {
            return m_solutionPath;
        }

        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator{ m_workspaceRoot, std::filesystem::directory_options::skip_permission_denied, error };
        std::filesystem::recursive_directory_iterator const end;
        for (; iterator != end && !error; iterator.increment(error))
        {
            auto const& entry = *iterator;

            if (entry.is_directory(error))
            {
                auto const name = entry.path().filename().wstring();
                if (name == L".git" || name == L".vs" || name == L"obj" || name == L"x64" || name == L"vcpkg_installed")
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }

            if (entry.is_regular_file(error) && entry.path().extension().wstring() == extension)
            {
                return entry.path();
            }
        }

        return {};
    }

    std::filesystem::path MainView::FindOwningProject(std::filesystem::path const& itemPath) const
    {
        if (itemPath.empty())
        {
            return {};
        }

        std::vector<std::filesystem::path> projects;
        for (auto const& path : m_workspaceFiles)
        {
            if (path.extension() == L".vcxproj")
            {
                projects.push_back(path);
            }
        }
        if (!m_startupProjectPath.empty())
        {
            std::stable_sort(projects.begin(), projects.end(), [this](auto const& left, auto const& right)
                             {
                                 return (left == m_startupProjectPath) > (right == m_startupProjectPath);
                             });
        }

        for (auto const& project : projects)
        {
            if (::VisualForge::Integration::ProjectWorkspaceService::ContainsItem(project, itemPath))
            {
                return project;
            }
        }
        return {};
    }

    std::filesystem::path MainView::FindBuiltExecutable(std::wstring_view configuration, std::wstring_view platform) const
    {
        auto const project = m_startupProjectPath.empty() ? FindWorkspaceFile(L".vcxproj") : m_startupProjectPath;
        if (project.empty())
        {
            return {};
        }

        auto const projectDirectory = project.parent_path();
        auto const projectName = project.stem();
        std::vector<std::filesystem::path> candidates{
            projectDirectory / platform / configuration / (projectName.wstring() + L".exe"),
            projectDirectory / platform / configuration / projectName / (projectName.wstring() + L".exe"),
            projectDirectory / configuration / platform / (projectName.wstring() + L".exe"),
            projectDirectory / configuration / (projectName.wstring() + L".exe")
        };

        for (auto const& candidate : candidates)
        {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error))
            {
                return candidate;
            }
        }

        return {};
    }

    winrt::Windows::Foundation::IAsyncAction MainView::BuildProjectAsync()
    {
        co_await RunBuildAsync(L"Build", L"Build solution", false);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RunBuildAsync(
        std::wstring target,
        std::wstring description,
        bool projectOnly)
    {
        if (m_buildRunning.exchange(true))
        {
            SetOutput(L"Build", L"A build is already running.");
            co_return;
        }
        m_buildCancelRequested = false;

        auto const solution = FindWorkspaceFile(L".slnx");
        auto const project = m_startupProjectPath.empty() ? FindWorkspaceFile(L".vcxproj") : m_startupProjectPath;
        auto const buildPath = projectOnly ? project : solution;
        if (buildPath.empty())
        {
            m_buildRunning = false;
            SetOutput(L"Build", projectOnly
                      ? L"Select a startup project before building the project."
                      : L"Open a workspace folder that contains a .slnx file before building.");
            co_return;
        }

        std::wstring configuration{ L"Debug" };
        std::wstring platform{ L"x64" };
        if (auto const item = ConfigurationComboBox().SelectedItem().try_as<ComboBoxItem>())
        {
            configuration = std::wstring{ unbox_value<hstring>(item.Content()) };
        }
        if (auto const item = PlatformComboBox().SelectedItem().try_as<ComboBoxItem>())
        {
            platform = std::wstring{ unbox_value<hstring>(item.Content()) };
        }

        ShellStatusText().Text(L"Building...");
        SetOutput(L"Build", description + L": " + buildPath.filename().wstring());
        OutputTextBox().Text(description + L": " + buildPath.filename().wstring() + L"...\n");
        auto const queue = DispatcherQueue();
        auto weakThis = get_weak();
        co_await winrt::resume_background();

        ::VisualForge::Integration::ProjectContext context;
        context.WorkspaceRoot = buildPath.parent_path();
        context.SolutionPath = projectOnly ? std::filesystem::path{} : solution;
        context.ProjectPath = projectOnly ? project : std::filesystem::path{};
        context.Configuration = std::move(configuration);
        context.Platform = std::move(platform);
        ::VisualForge::Integration::MSBuildAdapter adapter;
        auto command = target == L"Build" ? adapter.CreateBuildCommand(context)
            : target == L"Rebuild" ? adapter.CreateRebuildCommand(context)
            : adapter.CreateCleanCommand(context);
        ::VisualForge::Tool::ProcessSession process;
        auto const started = process.Start(command);
        while (started && process.IsRunning())
        {
            if (m_buildCancelRequested.load())
            {
                process.Stop();
                break;
            }
            auto snapshot = process.Snapshot();
            auto output = snapshot.StdOut + snapshot.StdErr;
            if (output.size() > 20000)
            {
                output.erase(0, output.size() - 20000);
            }
            auto wideOutput = std::wstring{ winrt::to_hstring(output) };
            (void)queue.TryEnqueue([weakThis, wideOutput = std::move(wideOutput)]
                                   {
                                       auto strongThis = weakThis.get();
                                       if (!strongThis)
                                       {
                                           return;
                                       }
                                       strongThis->OutputTextBox().Text(hstring{ wideOutput });
                                   });
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (started)
        {
            (void)process.WaitForExit();
        }
        auto snapshot = process.Snapshot();
        auto const cancelled = m_buildCancelRequested.load();

        auto output = snapshot.StdOut + snapshot.StdErr;
        if (output.size() > 20000)
        {
            output.erase(0, output.size() - 20000);
        }
        (void)queue.TryEnqueue([weakThis, started, cancelled, target = std::move(target), description = std::move(description), exitCode = snapshot.ExitCode, output = std::move(output)]
                               {
                                   auto strongThis = weakThis.get();
                                   if (!strongThis)
                                   {
                                       return;
                                   }
                                   strongThis->m_buildRunning = false;
                                   strongThis->m_buildCancelRequested = false;
                                   auto const wideOutput = started ? std::wstring{ winrt::to_hstring(output) } : L"MSBuild could not be started.";
                                   strongThis->OutputTextBox().Text(hstring{ wideOutput });
                                   strongThis->ParseBuildDiagnostics(wideOutput);
                                   strongThis->SetOutput(L"Build", description + L"\n" + wideOutput);
                                   strongThis->ShellStatusText().Text(cancelled ? L"Build cancelled" : started && exitCode == 0 ? target + L" succeeded" : target + L" failed");
                                   if (target == L"Build" && strongThis->m_launchAfterBuild)
                                   {
                                       auto const launchWithoutDebugging = strongThis->m_launchWithoutDebuggingAfterBuild;
                                       strongThis->m_launchAfterBuild = false;
                                       strongThis->m_launchWithoutDebuggingAfterBuild = false;
                                       if (!cancelled && started && exitCode == 0)
                                       {
                                           if (launchWithoutDebugging)
                                           {
                                               strongThis->RunWithoutDebugging_Click(*strongThis, RoutedEventArgs{});
                                           }
                                           else
                                           {
                                               strongThis->RunProject_Click(*strongThis, RoutedEventArgs{});
                                           }
                                       }
                                       else
                                       {
                                           strongThis->SetOutput(L"Run", L"Automatic build failed; target was not started.");
                                       }
                                   }
                               });
    }

    Microsoft::UI::Xaml::Visibility MainView::IsDebug()
    {
#ifdef _DEBUG
        return Microsoft::UI::Xaml::Visibility::Visible;
#else
        return Microsoft::UI::Xaml::Visibility::Collapsed;
#endif
    }

    winrt::Windows::Foundation::IAsyncAction MainView::LoadBackground()
    {
        auto& db = Core::AppSettingsDatabase::Instance();
        auto imagePath = db.GetStringW(Core::AppSettingsDatabase::CAT_UI, "background_image").value_or(L"");
        if (imagePath.empty())
        {
            this->Background(nullptr);
            co_return;
        }

        std::wstring imageUri = L"file:///" + imagePath;
        std::replace(imageUri.begin(), imageUri.end(), L'\\', L'/');

        auto const stretchIndex = std::clamp(static_cast<int>(db.GetInt(Core::AppSettingsDatabase::CAT_UI, "image_stretch", 3)), 0, 3);
        auto const opacity = std::clamp(db.GetDouble(Core::AppSettingsDatabase::CAT_UI, "image_opacity").value_or(20.0), 0.0, 100.0) / 100.0;

        auto brush = ImageBrush{};
        auto bitmap = BitmapImage{};
        bitmap.UriSource(winrt::Windows::Foundation::Uri{ imageUri });
        brush.ImageSource(bitmap);
        switch (stretchIndex)
        {
            case 1:
                brush.Stretch(Stretch::Fill);
                break;
            case 2:
                brush.Stretch(Stretch::Uniform);
                break;
            case 3:
                brush.Stretch(Stretch::UniformToFill);
                break;
            case 0:
            default:
                brush.Stretch(Stretch::None);
                break;
        }
        brush.Opacity(opacity);
        this->Background(brush);
        co_return;
    }

    void MainView::RootPage_PointerPressed(winrt::Windows::Foundation::IInspectable const& /*sender*/, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        auto props = e.GetCurrentPoint(nullptr).Properties();

        if (props.IsXButton1Pressed())
        {
            if (CanGoBack())
            {
                GoBack();
                e.Handled(true);
            }
        }
        else if (props.IsXButton2Pressed())
        {
            if (CanGoForward())
            {
                GoForward();
                e.Handled(true);
            }
        }
    }

    VisualForge::ViewModels::MainViewModel MainView::ViewModel()
    {
        return m_viewModel;
    }

    event_token MainView::CanGoBackChanged(TypedEventHandler<IInspectable, bool> const& handler)
    {
        return m_canGoBackChanged.add(handler);
    }

    void MainView::AppTitleBar_BackRequested(Microsoft::UI::Xaml::Controls::TitleBar const&, IInspectable const&)
    {
        GoBack();
    }

    void MainView::CanGoBackChanged(event_token const& token) noexcept
    {
        m_canGoBackChanged.remove(token);
    }

    void MainView::Navigate(hstring const& tag)
    {
        if (tag.empty())
        {
            return;
        }

        if (auto active = EditorTabs().SelectedItem().try_as<TabViewItem>())
        {
            if (auto found = m_documentTabs.find(winrt::get_abi(active)); found != m_documentTabs.end())
            {
                RecordNavigationPath(found->second.Path);
            }
        }
    }

    void MainView::GoBack()
    {
        if (m_navigationHistory.empty() || m_navigationIndex == 0)
        {
            return;
        }
        --m_navigationIndex;
        RestoreNavigationPathAsync(m_navigationHistory[m_navigationIndex]);
    }

    void MainView::GoForward()
    {
        if (m_navigationHistory.empty() || m_navigationIndex + 1 >= m_navigationHistory.size())
        {
            return;
        }
        ++m_navigationIndex;
        RestoreNavigationPathAsync(m_navigationHistory[m_navigationIndex]);
    }

    bool MainView::CanGoBack() const noexcept
    {
        return m_canGoBack;
    }

    bool MainView::CanGoForward() const noexcept
    {
        return m_canGoForward;
    }

    void MainView::RecordNavigationPath(std::filesystem::path const& path)
    {
        if (path.empty() || m_isRestoringNavigation)
        {
            return;
        }

        if (!m_navigationHistory.empty()
            && m_navigationIndex < m_navigationHistory.size()
            && m_navigationHistory[m_navigationIndex] == path)
        {
            UpdateNavigationState(m_navigationIndex > 0, m_navigationIndex + 1 < m_navigationHistory.size());
            return;
        }

        if (m_navigationIndex + 1 < m_navigationHistory.size())
        {
            m_navigationHistory.erase(m_navigationHistory.begin() + static_cast<std::ptrdiff_t>(m_navigationIndex + 1), m_navigationHistory.end());
        }
        m_navigationHistory.push_back(path);
        m_navigationIndex = m_navigationHistory.size() - 1;
        UpdateNavigationState(m_navigationIndex > 0, false);
    }

    winrt::Windows::Foundation::IAsyncAction MainView::RestoreNavigationPathAsync(std::filesystem::path const& path)
    {
        if (path.empty())
        {
            co_return;
        }
        m_isRestoringNavigation = true;
        co_await OpenPathAsync(path);
        m_isRestoringNavigation = false;
        UpdateNavigationState(m_navigationIndex > 0, m_navigationIndex + 1 < m_navigationHistory.size());
    }

    void MainView::DockTitleBar_PointerPressed(
        IInspectable const&,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        m_isDockDragPending = true;
        m_isDockDragFloating = false;
        ShowDockFloatingPreview(true);
        args.Handled(true);
    }

    void MainView::DockTitleBar_PointerMoved(
        IInspectable const&,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_isDockDragPending)
        {
            return;
        }

        auto const point = args.GetCurrentPoint(*this);
        if (point.Properties().IsLeftButtonPressed())
        {
            m_isDockDragFloating = true;
            ShowDockFloatingPreview(true);
            args.Handled(true);
        }
    }

    void MainView::DockTitleBar_PointerReleased(
        IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (m_isDockDragPending)
        {
            m_isDockDragPending = false;
            ShowDockFloatingPreview(false);
            if (m_isDockDragFloating)
            {
                OpenFloatingToolWindow(sender);
            }

            m_isDockDragFloating = false;
            args.Handled(true);
        }
    }

    void MainView::UpdateNavigationState(bool canGoBack, bool canGoForward)
    {
        if (m_canGoBack != canGoBack)
        {
            m_canGoBack = canGoBack;
            m_canGoBackChanged(*this, m_canGoBack);
        }

        m_canGoForward = canGoForward;
    }

    void MainView::RefreshStatusBar()
    {
        if (!m_viewModel)
        {
            return;
        }

        this->ShellStatusText().Text(L"Ready");
        this->LanguageServiceStatusText().Text(m_viewModel.LanguageServiceStatus());
        this->BuildStatusText().Text(m_viewModel.BuildStatus());
        this->DebugStatusText().Text(m_viewModel.DebugStatus());
        this->ProblemStatusText().Text(m_viewModel.ProblemsStatus());
    }

    void MainView::ShowDockFloatingPreview(bool visible)
    {
        this->FloatingPreviewHost().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainView::OpenFloatingToolWindow(IInspectable const& sender)
    {
        auto const element = sender.try_as<FrameworkElement>();
        if (!element)
        {
            return;
        }

        auto const tag = unbox_value_or<hstring>(element.Tag(), L"");
        if (tag.empty())
        {
            return;
        }

        auto contentId = std::wstring{ tag };
        auto title = std::wstring{ m_viewModel.ToolWindowTitle(tag) };
        auto const floated = winrt::get_self<winrt::VisualForge::ViewModels::implementation::MainViewModel>(m_viewModel)->Workspace().ExecuteDockCommand(contentId, ::VisualForge::IDE::Shell::DockCommandKind::Float);
        (void)floated;
        auto const opened = m_floatingWindowHost.Show({ contentId, title, 760, 520, true });
        (void)opened;
        RefreshStatusBar();
    }
}
