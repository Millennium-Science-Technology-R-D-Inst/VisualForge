#include "pch.h"

#include "IDE/Shell/FloatingWindowHost.h"

#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace VisualForge::IDE::Shell
{
    namespace
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using namespace winrt::Microsoft::UI::Xaml::Media;

        SolidColorBrush Brush(unsigned char r, unsigned char g, unsigned char b)
        {
            return SolidColorBrush{ winrt::Microsoft::UI::ColorHelper::FromArgb(255, r, g, b) };
        }

        TextBlock Label(std::wstring const& text, double fontSize = 12.0)
        {
            TextBlock block;
            block.Text(winrt::hstring{ text });
            block.FontSize(fontSize);
            block.TextWrapping(TextWrapping::Wrap);
            return block;
        }

        Button ToolButton(std::wstring const& text)
        {
            Button button;
            button.Content(winrt::box_value(winrt::hstring{ text }));
            button.Padding({ 10, 4, 10, 4 });
            return button;
        }

        TreeViewNode Node(std::wstring const& text, bool expanded = false)
        {
            TreeViewNode node;
            node.Content(winrt::box_value(winrt::hstring{ text }));
            node.IsExpanded(expanded);
            return node;
        }

        UIElement CreateSolutionExplorer()
        {
            Grid root;
            root.RowDefinitions().Append(RowDefinition{});
            root.RowDefinitions().GetAt(0).Height({ 0, GridUnitType::Auto });
            root.RowDefinitions().Append(RowDefinition{});

            AutoSuggestBox search;
            search.Margin({ 10, 10, 10, 6 });
            search.PlaceholderText(L"Search Solution Explorer");
            root.Children().Append(search);

            TreeView tree;
            tree.Margin({ 8, 0, 8, 8 });
            Grid::SetRow(tree, 1);
            auto solution = Node(L"Solution 'VisualForge' (1 of 1 project)", true);
            auto project = Node(L"VisualForge", true);
            project.Children().Append(Node(L"EditorCore", true));
            project.Children().Append(Node(L"IDE", true));
            project.Children().Append(Node(L"Integration"));
            project.Children().Append(Node(L"Tool"));
            project.Children().Append(Node(L"UI"));
            solution.Children().Append(project);
            tree.RootNodes().Append(solution);
            root.Children().Append(tree);
            return root;
        }

        UIElement CreateGitChanges()
        {
            StackPanel root;
            root.Padding({ 10, 10, 10, 10 });
            root.Spacing(8);
            StackPanel commands;
            commands.Orientation(Orientation::Horizontal);
            commands.Spacing(6);
            commands.Children().Append(ToolButton(L"Commit"));
            commands.Children().Append(ToolButton(L"Pull"));
            commands.Children().Append(ToolButton(L"Push"));
            root.Children().Append(commands);
            root.Children().Append(Label(L"Repository: main\nM  MainView.xaml\nM  MainViewModel.cpp\nA  IDE/Runtime/IdeRuntimeState.cpp", 12));
            return root;
        }

        UIElement CreateErrorList()
        {
            Grid root;
            root.RowDefinitions().Append(RowDefinition{});
            root.RowDefinitions().GetAt(0).Height({ 0, GridUnitType::Auto });
            root.RowDefinitions().Append(RowDefinition{});

            Grid header;
            header.Padding({ 10, 8, 10, 8 });
            header.ColumnDefinitions().Append(ColumnDefinition{});
            header.ColumnDefinitions().GetAt(0).Width({ 90, GridUnitType::Pixel });
            header.ColumnDefinitions().Append(ColumnDefinition{});
            header.ColumnDefinitions().Append(ColumnDefinition{});
            header.ColumnDefinitions().GetAt(2).Width({ 110, GridUnitType::Pixel });
            header.Children().Append(Label(L"Severity", 11));
            auto description = Label(L"Description", 11);
            Grid::SetColumn(description, 1);
            header.Children().Append(description);
            auto file = Label(L"File", 11);
            Grid::SetColumn(file, 2);
            header.Children().Append(file);
            root.Children().Append(header);

            ListView list;
            Grid::SetRow(list, 1);
            list.Items().Append(winrt::box_value(L"No issues. clangd diagnostics will appear here."));
            root.Children().Append(list);
            return root;
        }

        UIElement CreateOutput()
        {
            TextBlock output = Label(L"[Shell] Visual Studio style shell loaded.\n[MSBuild] evaluation output will stream here.\n[clangd] diagnostics and protocol events will stream here.\n[DAP] debugger adapter events will stream here.", 12);
            output.Margin({ 12, 12, 12, 12 });
            output.FontFamily(FontFamily{ L"Cascadia Mono" });
            return output;
        }

        UIElement CreateTerminal()
        {
            TextBlock terminal = Label(L"Developer PowerShell\nPS> msbuild src\\VisualForge\\VisualForge.slnx /p:Configuration=Debug /p:Platform=x64", 12);
            terminal.Margin({ 12, 12, 12, 12 });
            terminal.FontFamily(FontFamily{ L"Cascadia Mono" });
            return terminal;
        }

        UIElement CreateProperties()
        {
            Grid root;
            root.Margin({ 12, 12, 12, 12 });
            root.ColumnDefinitions().Append(ColumnDefinition{});
            root.ColumnDefinitions().GetAt(0).Width({ 130, GridUnitType::Pixel });
            root.ColumnDefinitions().Append(ColumnDefinition{});
            for (int row = 0; row < 4; ++row)
            {
                root.RowDefinitions().Append(RowDefinition{});
                root.RowDefinitions().GetAt(row).Height({ 30, GridUnitType::Pixel });
            }

            std::vector<std::pair<std::wstring, std::wstring>> values{
                { L"Selection", L"VisualForge" },
                { L"Kind", L"WinUI 3 C++/WinRT IDE" },
                { L"Project System", L"MSBuild + compile_commands" },
                { L"Debugger", L"DAP + LLDB" }
            };

            for (std::size_t index = 0; index < values.size(); ++index)
            {
                auto name = Label(values[index].first, 12);
                Grid::SetRow(name, static_cast<int>(index));
                root.Children().Append(name);
                auto value = Label(values[index].second, 12);
                Grid::SetRow(value, static_cast<int>(index));
                Grid::SetColumn(value, 1);
                root.Children().Append(value);
            }

            return root;
        }

        UIElement CreateOptions()
        {
            StackPanel root;
            root.Padding({ 12, 12, 12, 12 });
            root.Spacing(8);
            root.Children().Append(Label(L"Environment", 13));
            root.Children().Append(Label(L"Theme, keyboard profile, startup, window layout"));
            root.Children().Append(Label(L"Text Editor", 13));
            root.Children().Append(Label(L"DirectWrite rendering, virtualized text layout, search, undo/redo"));
            root.Children().Append(Label(L"Projects and Solutions", 13));
            root.Children().Append(Label(L"MSBuild evaluation, compile database generation, clangd integration"));
            return root;
        }

        UIElement CreateToolWindowBody(std::wstring const& contentId)
        {
            if (contentId == L"solutionExplorer")
            {
                return CreateSolutionExplorer();
            }

            if (contentId == L"gitChanges")
            {
                return CreateGitChanges();
            }

            if (contentId == L"errorList")
            {
                return CreateErrorList();
            }

            if (contentId == L"output")
            {
                return CreateOutput();
            }

            if (contentId == L"terminal")
            {
                return CreateTerminal();
            }

            if (contentId == L"properties")
            {
                return CreateProperties();
            }

            if (contentId == L"options")
            {
                return CreateOptions();
            }

            TextBlock fallback = Label(L"Tool window: " + contentId, 12);
            fallback.Margin({ 12, 12, 12, 12 });
            return fallback;
        }
    }

    bool FloatingWindowHost::Show(FloatingWindowDescriptor const& descriptor)
    {
        if (descriptor.ContentId.empty())
        {
            return false;
        }

        Window window;
        window.Title(winrt::hstring{ descriptor.Title.empty() ? descriptor.ContentId : descriptor.Title });

        Grid root;
        root.Background(Brush(21, 27, 34));
        root.RowDefinitions().Append(RowDefinition{});
        root.RowDefinitions().GetAt(0).Height({ 0, GridUnitType::Auto });
        root.RowDefinitions().Append(RowDefinition{});

        Grid titleBar;
        titleBar.Padding({ 8, 6, 8, 6 });
        titleBar.Background(Brush(27, 34, 44));
        titleBar.ColumnDefinitions().Append(ColumnDefinition{});
        titleBar.ColumnDefinitions().Append(ColumnDefinition{});
        titleBar.ColumnDefinitions().GetAt(1).Width({ 0, GridUnitType::Auto });
        TextBlock title;
        title.Text(winrt::hstring{ descriptor.Title.empty() ? descriptor.ContentId : descriptor.Title });
        title.VerticalAlignment(VerticalAlignment::Center);
        titleBar.Children().Append(title);
        StackPanel commands;
        commands.Orientation(Orientation::Horizontal);
        commands.Spacing(4);
        commands.Children().Append(ToolButton(L"Dock"));
        commands.Children().Append(ToolButton(L"Auto Hide"));
        commands.Children().Append(ToolButton(L"Close"));
        Grid::SetColumn(commands, 1);
        titleBar.Children().Append(commands);
        root.Children().Append(titleBar);

        Border body;
        body.BorderThickness({ 1, 1, 1, 1 });
        body.BorderBrush(Brush(48, 57, 71));
        body.Child(CreateToolWindowBody(descriptor.ContentId));
        Grid::SetRow(body, 1);
        root.Children().Append(body);

        window.Content(root);
        if (auto appWindow = window.AppWindow())
        {
            appWindow.Resize({ descriptor.Width, descriptor.Height });
        }

        window.Activate();
        m_windows.push_back(window);
        return true;
    }

    void FloatingWindowHost::CloseAll() noexcept
    {
        for (auto& window : m_windows)
        {
            try
            {
                window.Close();
            }
            catch (...)
            {
            }
        }

        m_windows.clear();
    }

    std::size_t FloatingWindowHost::Count() const noexcept
    {
        return m_windows.size();
    }
}
