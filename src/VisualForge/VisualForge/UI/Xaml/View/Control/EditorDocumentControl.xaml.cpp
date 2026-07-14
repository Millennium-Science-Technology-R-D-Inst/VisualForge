#include "pch.h"

#include "EditorDocumentControl.h"

#if __has_include("UI/Xaml/View/Control/EditorDocumentControl.g.cpp")
#include "UI/Xaml/View/Control/EditorDocumentControl.g.cpp"
#endif

#include <algorithm>
#include <cwctype>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>
#include <winrt/Microsoft.Graphics.Canvas.UI.Xaml.h>

using namespace winrt;
using namespace winrt::Microsoft::Graphics::Canvas::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::VisualForge::UI::Xaml::View::Control::implementation
{
    namespace
    {
        constexpr float LineHeight = 20.0f;
        constexpr float GutterWidth = 56.0f;
        constexpr float CharacterWidth = 7.82f;
        constexpr Windows::UI::Color LineNumberColor{ 0xFF, 0x6B, 0x76, 0x86 };
        constexpr Windows::UI::Color TextColor{ 0xD6, 0xDE, 0xEB, 0xFF };
        constexpr Windows::UI::Color GutterColor{ 0xFF, 0x18, 0x20, 0x2A };
        constexpr Windows::UI::Color SelectionColor{ 0xFF, 0x26, 0x4F, 0x78 };
        constexpr Windows::UI::Color CaretColor{ 0xFF, 0x5D, 0xD6, 0xFF };
        constexpr Windows::UI::Color KeywordColor{ 0xFF, 0xC5, 0x86, 0xC0 };
        constexpr Windows::UI::Color StringColor{ 0xFF, 0xCE, 0x91, 0x78 };
        constexpr Windows::UI::Color CommentColor{ 0xFF, 0x6A, 0x99, 0x55 };
        constexpr Windows::UI::Color NumberColor{ 0xFF, 0xB5, 0xCE, 0xA8 };

        bool IsKeyword(std::wstring_view token)
        {
            constexpr std::wstring_view keywords[] = {
                L"alignas", L"auto", L"bool", L"break", L"case", L"catch", L"class",
                L"const", L"constexpr", L"continue", L"co_await", L"co_return",
                L"decltype", L"default", L"delete", L"do", L"else", L"enum",
                L"explicit", L"export", L"extern", L"false", L"for", L"friend",
                L"if", L"import", L"inline", L"namespace", L"new", L"noexcept",
                L"nullptr", L"operator", L"private", L"protected", L"public",
                L"return", L"sizeof", L"static", L"struct", L"switch", L"template",
                L"this", L"throw", L"true", L"try", L"typedef", L"typename",
                L"using", L"virtual", L"void", L"while", L"xmlns", L"x:Name"
            };
            for (auto const keyword : keywords)
            {
                if (token == keyword)
                {
                    return true;
                }
            }
            return false;
        }
    }

    EditorDocumentControl::EditorDocumentControl()
    {
    }

    hstring EditorDocumentControl::Text() const
    {
        return hstring{ m_text };
    }

    int32_t EditorDocumentControl::SelectionStart() const
    {
        return m_selectionStart;
    }

    int32_t EditorDocumentControl::SelectionLength() const
    {
        return m_selectionLength;
    }

    int32_t EditorDocumentControl::ScrollLine() const
    {
        return static_cast<int32_t>(m_scrollLine);
    }

    int32_t EditorDocumentControl::ScrollColumn() const
    {
        return static_cast<int32_t>(m_scrollColumn);
    }

    void EditorDocumentControl::SetText(hstring const& text)
    {
        m_isLoading = true;
        m_text = text.c_str();
        InputTextBox().Text(text);
        InputTextBox().Select(0, 0);
        m_selectionStart = 0;
        m_selectionLength = 0;
        m_selectionAnchor = 0;
        m_isPointerSelecting = false;
        m_semanticTokens.clear();
        m_isLoading = false;
        m_scrollLine = 0;
        m_scrollColumn = 0;
        m_breakpointLines.clear();
        m_diagnosticLines.clear();
        m_debugLine = 0;
        UpdateScrollBars();
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::ReplaceSelection(hstring const& text)
    {
        auto const start = static_cast<std::size_t>((std::max)(0, m_selectionStart));
        auto const length = static_cast<std::size_t>((std::max)(0, m_selectionLength));
        if (start > m_text.size())
        {
            return;
        }

        auto const removedLength = (std::min)(length, m_text.size() - start);
        AdjustLineMarkersForEdit(start, removedLength, text.c_str());
        auto const breakpoints = m_breakpointLines;
        auto const diagnostics = m_diagnosticLines;
        auto const debugLine = m_debugLine;
        m_text.replace(start, removedLength, text.c_str());
        auto const next = start + text.size();
        SetText(hstring{ m_text });
        m_breakpointLines = breakpoints;
        m_diagnosticLines = diagnostics;
        m_debugLine = debugLine;
        EditorCanvas().Invalidate();
        Select(static_cast<int32_t>(next), 0);
        RaiseTextChanged();
    }

    void EditorDocumentControl::Select(int32_t start, int32_t length)
    {
        auto const boundedStart = (std::min)(static_cast<std::size_t>((std::max)(0, start)), m_text.size());
        auto const boundedLength = (std::min)(static_cast<std::size_t>((std::max)(0, length)), m_text.size() - boundedStart);
        InputTextBox().Select(static_cast<int32_t>(boundedStart), static_cast<int32_t>(boundedLength));
        m_selectionStart = static_cast<int32_t>(boundedStart);
        m_selectionLength = static_cast<int32_t>(boundedLength);
        EnsureCaretVisible();
        EditorCanvas().Invalidate();
        RaiseSelectionChanged();
    }

    void EditorDocumentControl::SetScrollPosition(int32_t line, int32_t column)
    {
        m_scrollLine = static_cast<std::size_t>((std::max)(0, line));
        m_scrollColumn = static_cast<std::size_t>((std::max)(0, column));
        UpdateScrollBars();
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::FocusEditor()
    {
        InputTextBox().Focus(FocusState::Programmatic);
    }

    void EditorDocumentControl::SetBreakpoint(int32_t line, bool enabled)
    {
        if (line <= 0)
        {
            return;
        }
        if (enabled)
        {
            m_breakpointLines.insert(line);
        }
        else
        {
            m_breakpointLines.erase(line);
        }
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::SetDebugLine(int32_t line)
    {
        m_debugLine = (std::max)(0, line);
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::SetSemanticTokens(hstring const& tokenData)
    {
        m_semanticTokens.clear();
        auto const data = std::wstring{ tokenData };
        std::size_t cursor{};
        while (cursor < data.size())
        {
            auto const end = data.find(L'|', cursor);
            auto const entry = data.substr(cursor, end == std::wstring::npos ? std::wstring::npos : end - cursor);
            std::size_t fields[4]{};
            std::size_t fieldStart{};
            bool valid = true;
            for (std::size_t index = 0; index < 4; ++index)
            {
                auto const separator = entry.find(L',', fieldStart);
                auto const field = entry.substr(fieldStart, separator == std::wstring::npos ? std::wstring::npos : separator - fieldStart);
                try
                {
                    fields[index] = static_cast<std::size_t>(std::stoull(field));
                }
                catch (...)
                {
                    valid = false;
                    break;
                }
                fieldStart = separator == std::wstring::npos ? entry.size() : separator + 1;
            }
            if (valid && fields[2] > 0)
            {
                m_semanticTokens.push_back({ fields[0], fields[1], fields[2], fields[3] });
            }
            if (end == std::wstring::npos)
            {
                break;
            }
            cursor = end + 1;
        }
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::SetDiagnosticLine(int32_t line, bool enabled)
    {
        if (line <= 0)
        {
            return;
        }
        if (enabled)
        {
            m_diagnosticLines.insert(line);
        }
        else
        {
            m_diagnosticLines.erase(line);
        }
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::ClearDiagnosticLines()
    {
        m_diagnosticLines.clear();
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::CopySelection()
    {
        if (m_selectionLength <= 0)
        {
            return;
        }

        auto const start = static_cast<std::size_t>((std::max)(0, m_selectionStart));
        auto const length = static_cast<std::size_t>((std::max)(0, m_selectionLength));
        if (start > m_text.size() || length > m_text.size() - start)
        {
            return;
        }

        Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.RequestedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
        package.SetText(hstring{ m_text.substr(start, length) });
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    }

    void EditorDocumentControl::CutSelection()
    {
        if (m_selectionLength <= 0)
        {
            return;
        }
        CopySelection();
        ReplaceSelection(hstring{});
    }

    winrt::Windows::Foundation::IAsyncAction EditorDocumentControl::PasteClipboardAsync()
    {
        auto view = Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
        if (!view || !view.Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text()))
        {
            co_return;
        }

        auto text = co_await view.GetTextAsync();
        if (!text.empty())
        {
            ReplaceSelection(text);
        }
    }

    void EditorDocumentControl::SelectAllText()
    {
        Select(0, static_cast<int32_t>(m_text.size()));
        FocusEditor();
    }

    event_token EditorDocumentControl::TextChanged(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, Windows::Foundation::IInspectable> const& handler)
    {
        return m_textChanged.add(handler);
    }

    void EditorDocumentControl::TextChanged(event_token const& token) noexcept
    {
        m_textChanged.remove(token);
    }

    event_token EditorDocumentControl::SelectionChanged(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, Windows::Foundation::IInspectable> const& handler)
    {
        return m_selectionChanged.add(handler);
    }

    void EditorDocumentControl::SelectionChanged(event_token const& token) noexcept
    {
        m_selectionChanged.remove(token);
    }

    event_token EditorDocumentControl::CompletionRequested(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, Windows::Foundation::IInspectable> const& handler)
    {
        return m_completionRequested.add(handler);
    }

    void EditorDocumentControl::CompletionRequested(event_token const& token) noexcept
    {
        m_completionRequested.remove(token);
    }

    event_token EditorDocumentControl::BreakpointRequested(
        Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable, int32_t> const& handler)
    {
        return m_breakpointRequested.add(handler);
    }

    void EditorDocumentControl::BreakpointRequested(event_token const& token) noexcept
    {
        m_breakpointRequested.remove(token);
    }

    void EditorDocumentControl::CompletionKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        if (m_completionRequested)
        {
            m_completionRequested(*this, nullptr);
        }
    }

    void EditorDocumentControl::DuplicateLineKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        DuplicateCurrentLine();
    }

    void EditorDocumentControl::DeleteLineKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        DeleteCurrentLine();
    }

    void EditorDocumentControl::ToggleCommentKeyboardAccelerator_Invoked(
        Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(true);
        ToggleComment();
    }

    void EditorDocumentControl::InputTextBox_TextChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (m_isLoading)
        {
            return;
        }

        auto const nextText = std::wstring{ InputTextBox().Text() };
        if (nextText != m_text)
        {
            std::size_t prefix{};
            while (prefix < m_text.size() && prefix < nextText.size() && m_text[prefix] == nextText[prefix])
            {
                ++prefix;
            }

            std::size_t suffix{};
            while (suffix < m_text.size() - prefix && suffix < nextText.size() - prefix
                && m_text[m_text.size() - 1 - suffix] == nextText[nextText.size() - 1 - suffix])
            {
                ++suffix;
            }
            AdjustLineMarkersForEdit(prefix, m_text.size() - prefix - suffix,
                std::wstring_view{ nextText }.substr(prefix, nextText.size() - prefix - suffix));
        }
        m_text = nextText;
        EnsureCaretVisible();
        EditorCanvas().Invalidate();
        RaiseTextChanged();
    }

    void EditorDocumentControl::InputTextBox_KeyDown(
        IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == Windows::System::VirtualKey::Tab)
        {
            if ((::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                auto const offset = (std::min)(
                    static_cast<std::size_t>((std::max)(0, m_selectionStart)), m_text.size());
                auto const lineStart = offset == 0 ? 0 : m_text.rfind(L'\n', offset - 1) + 1;
                auto const indentation = m_text.substr(lineStart, offset - lineStart);
                auto const removeLength = indentation.find_first_not_of(L' ') == std::wstring::npos
                    ? (std::min)(std::size_t{ 4 }, indentation.size())
                    : 0;
                if (removeLength > 0)
                {
                    Select(static_cast<int32_t>(lineStart), static_cast<int32_t>(removeLength));
                    ReplaceSelection(hstring{});
                }
                args.Handled(true);
                return;
            }
            ReplaceSelection(hstring{ L"    " });
            args.Handled(true);
            return;
        }

        if (args.Key() == Windows::System::VirtualKey::Back && m_selectionLength == 0)
        {
            auto const offset = static_cast<std::size_t>((std::max)(0, m_selectionStart));
            if (offset > 0)
            {
                auto const lineStart = offset == 0 ? 0 : m_text.rfind(L'\n', offset - 1) + 1;
                auto const linePrefix = m_text.substr(lineStart, offset - lineStart);
                if (!linePrefix.empty()
                    && linePrefix.find_first_not_of(L' ') == std::wstring::npos)
                {
                    auto const removeLength = (std::min)(std::size_t{ 4 }, linePrefix.size());
                    Select(static_cast<int32_t>(offset - removeLength), static_cast<int32_t>(removeLength));
                    ReplaceSelection(hstring{});
                    args.Handled(true);
                    return;
                }
            }
        }

        if (args.Key() != Windows::System::VirtualKey::Enter || m_selectionLength != 0)
        {
            return;
        }

        auto const offset = static_cast<std::size_t>((std::max)(0, m_selectionStart));
        auto const lineStart = offset == 0 ? 0 : m_text.rfind(L'\n', offset - 1) + 1;
        auto const linePrefix = m_text.substr(lineStart, offset - lineStart);
        std::size_t indentLength{};
        while (indentLength < linePrefix.size()
            && (linePrefix[indentLength] == L' ' || linePrefix[indentLength] == L'\t'))
        {
            ++indentLength;
        }

        auto indentation = linePrefix.substr(0, indentLength);
        auto contentEnd = linePrefix.find_last_not_of(L" \t\r");
        if (contentEnd != std::wstring::npos && linePrefix[contentEnd] == L'{')
        {
            indentation += L"    ";
        }

        ReplaceSelection(hstring{ L"\n" + indentation });
        args.Handled(true);
    }

    void EditorDocumentControl::InputTextBox_PointerPressed(
        IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const offset = OffsetFromPoint(args.GetCurrentPoint(InputTextBox()).Position());
        m_selectionAnchor = offset;
        m_isPointerSelecting = true;
        InputTextBox().CapturePointer(args.Pointer());
        Select(static_cast<int32_t>(offset), 0);
        FocusEditor();
        args.Handled(true);
    }

    void EditorDocumentControl::InputTextBox_PointerMoved(
        IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (!m_isPointerSelecting)
        {
            return;
        }

        auto const offset = OffsetFromPoint(args.GetCurrentPoint(InputTextBox()).Position());
        auto const start = (std::min)(m_selectionAnchor, offset);
        auto const length = (std::max)(m_selectionAnchor, offset) - start;
        Select(static_cast<int32_t>(start), static_cast<int32_t>(length));
        args.Handled(true);
    }

    void EditorDocumentControl::InputTextBox_PointerReleased(
        IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (m_isPointerSelecting)
        {
            m_isPointerSelecting = false;
            InputTextBox().ReleasePointerCapture(args.Pointer());
            args.Handled(true);
        }
    }

    void EditorDocumentControl::InputTextBox_SelectionChanged(IInspectable const&, RoutedEventArgs const&)
    {
        m_selectionStart = InputTextBox().SelectionStart();
        m_selectionLength = InputTextBox().SelectionLength();
        if (!m_isLoading)
        {
            EnsureCaretVisible();
            EditorCanvas().Invalidate();
            RaiseSelectionChanged();
        }
    }

    void EditorDocumentControl::ContextCut_Click(IInspectable const&, RoutedEventArgs const&)
    {
        CutSelection();
        FocusEditor();
    }

    void EditorDocumentControl::ContextCopy_Click(IInspectable const&, RoutedEventArgs const&)
    {
        CopySelection();
        FocusEditor();
    }

    void EditorDocumentControl::ContextPaste_Click(IInspectable const&, RoutedEventArgs const&)
    {
        PasteClipboardAsync();
        FocusEditor();
    }

    void EditorDocumentControl::ContextSelectAll_Click(IInspectable const&, RoutedEventArgs const&)
    {
        SelectAllText();
        FocusEditor();
    }

    void EditorDocumentControl::EditorDocumentControl_PointerWheelChanged(
        IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const delta = args.GetCurrentPoint(*this).Properties().MouseWheelDelta();
        auto const visibleLines = (std::max)(1u, static_cast<unsigned int>(EditorCanvas().ActualHeight() / LineHeight));
        auto const maxScroll = LineCount() > visibleLines ? LineCount() - visibleLines : 0;
        if (delta < 0)
        {
            m_scrollLine = (std::min)(m_scrollLine + 3, maxScroll);
        }
        else if (delta > 0)
        {
            m_scrollLine = m_scrollLine > 3 ? m_scrollLine - 3 : 0;
        }
        UpdateScrollBars();
        EditorCanvas().Invalidate();
        args.Handled(true);
    }

    void EditorDocumentControl::EditorCanvas_SizeChanged(
        IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        if (!EditorVerticalScrollBar() || !EditorHorizontalScrollBar())
        {
            return;
        }
        UpdateScrollBars();
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::EditorVerticalScrollBar_ValueChanged(
        IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        if (m_isUpdatingScrollBars)
        {
            return;
        }
        m_scrollLine = static_cast<std::size_t>((std::max)(0.0, args.NewValue()));
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::EditorHorizontalScrollBar_ValueChanged(
        IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        if (m_isUpdatingScrollBars)
        {
            return;
        }
        m_scrollColumn = static_cast<std::size_t>((std::max)(0.0, args.NewValue()));
        EditorCanvas().Invalidate();
    }

    void EditorDocumentControl::EditorCanvas_PointerPressed(
        IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const point = args.GetCurrentPoint(EditorCanvas());
        if (point.Position().X > GutterWidth)
        {
            return;
        }

        auto const line = static_cast<int32_t>(
            m_scrollLine + static_cast<std::size_t>((std::max)(0.0f, point.Position().Y) / LineHeight) + 1);
        RaiseBreakpointRequested(line);
        args.Handled(true);
    }

    std::size_t EditorDocumentControl::LineCount() const noexcept
    {
        return static_cast<std::size_t>(std::count(m_text.begin(), m_text.end(), L'\n')) + 1;
    }

    std::size_t EditorDocumentControl::OffsetFromPoint(Windows::Foundation::Point const& point)
    {
        auto const visibleLine = static_cast<std::size_t>(
            (std::max)(0.0f, point.Y - 8.0f) / LineHeight);
        auto const requestedLine = (std::min)(m_scrollLine + visibleLine, LineCount() - 1);

        std::size_t lineStart = 0;
        for (std::size_t line = 0; line < requestedLine; ++line)
        {
            auto const newline = m_text.find(L'\n', lineStart);
            if (newline == std::wstring::npos)
            {
                lineStart = m_text.size();
                break;
            }
            lineStart = newline + 1;
        }

        auto const lineEnd = m_text.find(L'\n', lineStart);
        auto const lineLength = (lineEnd == std::wstring::npos ? m_text.size() : lineEnd) - lineStart;
        auto const line = m_text.substr(lineStart, lineLength);
        auto requestedColumn = m_scrollColumn + static_cast<std::size_t>(
            (std::max)(0.0f, point.X - GutterWidth - 10.0f) / CharacterWidth);
        if (auto layout = m_dwrite.CreateTextLayout(line, 100000.0f, LineHeight))
        {
            FLOAT scrollOrigin{};
            if (m_scrollColumn > 0)
            {
                DWRITE_HIT_TEST_METRICS scrollMetrics{};
                FLOAT scrollY{};
                if (SUCCEEDED(layout->HitTestTextPosition(
                    static_cast<UINT32>((std::min)(m_scrollColumn, lineLength)), FALSE,
                    &scrollOrigin, &scrollY, &scrollMetrics)))
                {
                    scrollOrigin = (std::max)(0.0f, scrollOrigin);
                }
            }

            BOOL isTrailing{};
            BOOL isInside{};
            DWRITE_HIT_TEST_METRICS hitMetrics{};
            FLOAT hitX = (std::max)(0.0f, point.X - GutterWidth - 10.0f) + scrollOrigin;
            FLOAT hitY{};
            if (SUCCEEDED(layout->HitTestPoint(
                hitX, (std::max)(0.0f, point.Y - 8.0f), &isTrailing, &isInside, &hitMetrics)))
            {
                (void)hitY;
                requestedColumn = static_cast<std::size_t>(hitMetrics.textPosition) + (isTrailing ? 1u : 0u);
            }
        }
        return lineStart + (std::min)(requestedColumn, lineLength);
    }

    std::size_t EditorDocumentControl::LongestLineLength() const noexcept
    {
        std::size_t longest{};
        std::size_t start{};
        while (start <= m_text.size())
        {
            auto const end = m_text.find(L'\n', start);
            longest = (std::max)(longest, (end == std::wstring::npos ? m_text.size() : end) - start);
            if (end == std::wstring::npos)
            {
                break;
            }
            start = end + 1;
        }
        return longest;
    }

    void EditorDocumentControl::EnsureCaretVisible()
    {
        auto const offset = (std::min)(static_cast<std::size_t>((std::max)(0, m_selectionStart)), m_text.size());
        auto const line = static_cast<std::size_t>(std::count(m_text.begin(), m_text.begin() + static_cast<std::ptrdiff_t>(offset), L'\n'));
        auto const lineStart = offset == 0 ? std::wstring::npos : m_text.rfind(L'\n', offset - 1);
        auto const column = offset - (lineStart == std::wstring::npos ? 0 : lineStart + 1);
        auto const visibleLines = (std::max)(1u, static_cast<unsigned int>(EditorCanvas().ActualHeight() / LineHeight));
        if (line < m_scrollLine)
        {
            m_scrollLine = line;
        }
        else if (line >= m_scrollLine + visibleLines)
        {
            m_scrollLine = line - visibleLines + 1;
        }

        auto const visibleColumns = (std::max)(1u, static_cast<unsigned int>((EditorCanvas().ActualWidth() - GutterWidth - 14.0f) / CharacterWidth));
        if (column < m_scrollColumn)
        {
            m_scrollColumn = column;
        }
        else if (column >= m_scrollColumn + visibleColumns)
        {
            m_scrollColumn = column - visibleColumns + 1;
        }
        UpdateScrollBars();
    }

    void EditorDocumentControl::UpdateScrollBars()
    {
        if (!EditorVerticalScrollBar() || !EditorHorizontalScrollBar())
        {
            return;
        }
        auto const visibleLines = (std::max)(1u, static_cast<unsigned int>(EditorCanvas().ActualHeight() / LineHeight));
        auto const maxLine = LineCount() > visibleLines ? LineCount() - visibleLines : 0;
        auto const visibleColumns = (std::max)(1u, static_cast<unsigned int>(
            (EditorCanvas().ActualWidth() - GutterWidth - 14.0f) / CharacterWidth));
        auto const longestLine = LongestLineLength();
        auto const maxColumn = longestLine > visibleColumns ? longestLine - visibleColumns : 0;
        m_scrollLine = (std::min)(m_scrollLine, maxLine);
        m_scrollColumn = (std::min)(m_scrollColumn, maxColumn);

        m_isUpdatingScrollBars = true;
        EditorVerticalScrollBar().Minimum(0.0);
        EditorVerticalScrollBar().Maximum(static_cast<double>(maxLine));
        EditorVerticalScrollBar().ViewportSize(static_cast<double>(visibleLines));
        EditorVerticalScrollBar().SmallChange(1.0);
        EditorVerticalScrollBar().LargeChange(static_cast<double>(visibleLines));
        EditorVerticalScrollBar().Value(static_cast<double>(m_scrollLine));
        EditorHorizontalScrollBar().Minimum(0.0);
        EditorHorizontalScrollBar().Maximum(static_cast<double>(maxColumn));
        EditorHorizontalScrollBar().ViewportSize(static_cast<double>(visibleColumns));
        EditorHorizontalScrollBar().SmallChange(1.0);
        EditorHorizontalScrollBar().LargeChange(static_cast<double>(visibleColumns));
        EditorHorizontalScrollBar().Value(static_cast<double>(m_scrollColumn));
        m_isUpdatingScrollBars = false;
    }

    void EditorDocumentControl::AdjustLineMarkersForEdit(
        std::size_t start,
        std::size_t removedLength,
        std::wstring_view insertedText)
    {
        if (start > m_text.size())
        {
            return;
        }

        auto const boundedEnd = (std::min)(m_text.size(), start + removedLength);
        auto const lineAt = [this](std::size_t offset)
        {
            return static_cast<int32_t>(std::count(m_text.begin(),
                m_text.begin() + static_cast<std::ptrdiff_t>((std::min)(offset, m_text.size())), L'\n')) + 1;
        };
        auto const firstLine = lineAt(start);
        auto const lastLine = lineAt(boundedEnd);
        auto const removedLines = static_cast<int32_t>(std::count(
            m_text.begin() + static_cast<std::ptrdiff_t>(start),
            m_text.begin() + static_cast<std::ptrdiff_t>(boundedEnd), L'\n'));
        auto const insertedLines = static_cast<int32_t>(std::count(insertedText.begin(), insertedText.end(), L'\n'));
        auto const lineDelta = insertedLines - removedLines;
        auto const atLineStart = start == 0 || m_text[start - 1] == L'\n';

        auto shift = [&](std::set<int32_t>& markers)
        {
            std::set<int32_t> shifted;
            for (auto line : markers)
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
                    shifted.insert(line);
                }
            }
            markers = std::move(shifted);
        };

        shift(m_breakpointLines);
        shift(m_diagnosticLines);
        if (m_debugLine > 0)
        {
            std::set<int32_t> debugLine{ m_debugLine };
            shift(debugLine);
            m_debugLine = debugLine.empty() ? 0 : *debugLine.begin();
        }
    }

    void EditorDocumentControl::DuplicateCurrentLine()
    {
        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, m_selectionStart)), m_text.size());
        auto const lineStart = offset == 0 ? 0 : m_text.rfind(L'\n', offset - 1) + 1;
        auto const lineEnd = m_text.find(L'\n', lineStart);
        auto const end = lineEnd == std::wstring::npos ? m_text.size() : lineEnd;
        auto const line = m_text.substr(lineStart, end - lineStart);
        Select(static_cast<int32_t>(end), 0);
        ReplaceSelection(hstring{ L"\n" + line });
    }

    void EditorDocumentControl::DeleteCurrentLine()
    {
        if (m_text.empty())
        {
            return;
        }

        auto const offset = (std::min)(
            static_cast<std::size_t>((std::max)(0, m_selectionStart)), m_text.size());
        auto const lineStart = offset == 0 ? 0 : m_text.rfind(L'\n', offset - 1) + 1;
        auto const lineEnd = m_text.find(L'\n', lineStart);
        std::size_t removeStart = lineStart;
        std::size_t removeLength{};
        if (lineEnd != std::wstring::npos)
        {
            removeLength = lineEnd + 1 - lineStart;
        }
        else if (lineStart > 0)
        {
            removeStart = lineStart - 1;
            removeLength = m_text.size() - removeStart;
        }
        else
        {
            removeLength = m_text.size();
        }

        Select(static_cast<int32_t>(removeStart), static_cast<int32_t>(removeLength));
        ReplaceSelection(hstring{});
    }

    void EditorDocumentControl::ToggleComment()
    {
        if (m_text.empty())
        {
            return;
        }

        auto const selectionStart = (std::min)(
            static_cast<std::size_t>((std::max)(0, m_selectionStart)), m_text.size());
        auto const selectionEnd = (std::min)(
            selectionStart + static_cast<std::size_t>((std::max)(0, m_selectionLength)), m_text.size());
        auto const rangeStart = selectionStart == 0 ? 0 : m_text.rfind(L'\n', selectionStart - 1) + 1;
        auto const selectedLineEnd = m_text.find(L'\n', selectionEnd);
        auto const rangeEnd = selectedLineEnd == std::wstring::npos ? m_text.size() : selectedLineEnd;
        auto replacement = m_text.substr(rangeStart, rangeEnd - rangeStart);

        bool hasCode{};
        bool allCommented{ true };
        std::size_t lineStart{};
        while (lineStart <= replacement.size())
        {
            auto const lineEnd = replacement.find(L'\n', lineStart);
            auto const end = lineEnd == std::wstring::npos ? replacement.size() : lineEnd;
            auto const first = replacement.find_first_not_of(L" \t", lineStart);
            if (first != std::wstring::npos && first < end)
            {
                hasCode = true;
                if (first + 2 > end || replacement.compare(first, 2, L"//") != 0)
                {
                    allCommented = false;
                }
            }
            if (lineEnd == std::wstring::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }

        if (!hasCode)
        {
            return;
        }

        std::wstring updated;
        updated.reserve(replacement.size() + 3);
        lineStart = 0;
        while (lineStart <= replacement.size())
        {
            auto const lineEnd = replacement.find(L'\n', lineStart);
            auto const end = lineEnd == std::wstring::npos ? replacement.size() : lineEnd;
            auto const first = replacement.find_first_not_of(L" \t", lineStart);
            if (allCommented && first != std::wstring::npos && first + 2 <= end
                && replacement.compare(first, 2, L"//") == 0)
            {
                updated.append(replacement, lineStart, first - lineStart);
                auto const contentStart = first + 2 < end && replacement[first + 2] == L' ' ? first + 3 : first + 2;
                updated.append(replacement, contentStart, end - contentStart);
            }
            else
            {
                auto const contentStart = first == std::wstring::npos || first >= end ? end : first;
                updated.append(replacement, lineStart, contentStart - lineStart);
                if (contentStart < end)
                {
                    updated += L"// ";
                    updated.append(replacement, contentStart, end - contentStart);
                }
            }
            if (lineEnd == std::wstring::npos)
            {
                break;
            }
            updated += L'\n';
            lineStart = lineEnd + 1;
        }

        Select(static_cast<int32_t>(rangeStart), static_cast<int32_t>(rangeEnd - rangeStart));
        ReplaceSelection(hstring{ updated });
    }

    void EditorDocumentControl::EditorCanvas_Draw(CanvasControl const&, CanvasDrawEventArgs const& args)
    {
        auto drawingSession = args.DrawingSession();
        drawingSession.Clear(GutterColor);
        drawingSession.FillRectangle(GutterWidth, 0.0f, 100000.0f, 100000.0f, Windows::UI::Color{ 0xFF, 0x10, 0x15, 0x1D });

        auto format = Microsoft::Graphics::Canvas::Text::CanvasTextFormat{};
        format.FontFamily(L"Cascadia Mono");
        format.FontSize(13.0f);
        format.WordWrapping(Microsoft::Graphics::Canvas::Text::CanvasWordWrapping::NoWrap);

        std::size_t lineNumber = 1;
        std::size_t lineStart = 0;
        while (lineStart <= m_text.size())
        {
            auto const lineEnd = m_text.find(L'\n', lineStart);
            auto const end = lineEnd == std::wstring::npos ? m_text.size() : lineEnd;
            auto line = m_text.substr(lineStart, end - lineStart);

            if (lineNumber <= m_scrollLine)
            {
                ++lineNumber;
                if (lineEnd == std::wstring::npos)
                {
                    break;
                }
                lineStart = lineEnd + 1;
                continue;
            }

            auto const visibleLine = lineNumber - m_scrollLine - 1;
            auto const isDebugLine = m_debugLine == static_cast<int32_t>(lineNumber);
            if (isDebugLine)
            {
                drawingSession.FillRectangle(
                    GutterWidth,
                    static_cast<float>(visibleLine) * LineHeight,
                    100000.0f,
                    LineHeight,
                    Windows::UI::Color{ 0xFF, 0x3A, 0x3B, 0x22 });
            }
            if (m_breakpointLines.contains(static_cast<int32_t>(lineNumber)))
            {
                drawingSession.FillEllipse(
                    25.0f,
                    static_cast<float>(visibleLine) * LineHeight + 10.0f,
                    4.5f,
                    4.5f,
                    Windows::UI::Color{ 0xFF, 0xE5, 0x5B, 0x5B });
            }
            if (m_diagnosticLines.contains(static_cast<int32_t>(lineNumber)))
            {
                auto const underlineY = static_cast<float>(visibleLine * LineHeight) + LineHeight - 2.0f;
                for (float x = GutterWidth + 8.0f; x < EditorCanvas().ActualWidth(); x += 6.0f)
                {
                    drawingSession.DrawLine(
                        x,
                        underlineY,
                        (std::min)(x + 3.0f, static_cast<float>(EditorCanvas().ActualWidth())),
                        underlineY - 1.5f,
                        Windows::UI::Color{ 0xFF, 0xF1, 0x5B, 0x5B },
                        1.2f);
                }
            }
            auto const selectionBegin = static_cast<std::size_t>((std::max)(0, m_selectionStart));
            auto const selectionEnd = selectionBegin + static_cast<std::size_t>((std::max)(0, m_selectionLength));
            auto const currentLineEnd = end;
            auto const selectedBegin = (std::max)(selectionBegin, lineStart);
            auto const selectedEnd = (std::min)(selectionEnd, currentLineEnd);
            if (selectedBegin < selectedEnd)
            {
                auto const left = GutterWidth + 10.0f + static_cast<float>(selectedBegin - lineStart) * CharacterWidth - static_cast<float>(m_scrollColumn) * CharacterWidth;
                auto const width = static_cast<float>(selectedEnd - selectedBegin) * CharacterWidth;
                drawingSession.FillRectangle(left, static_cast<float>(visibleLine) * LineHeight, width, LineHeight, SelectionColor);
            }

            // DWriteCore remains the measurement source while Win2D presents the pixels.
            auto layout = m_dwrite.CreateTextLayout(line, 100000.0f, LineHeight);
            float dwriteScrollX{};
            if (layout && m_scrollColumn < line.size())
            {
                DWRITE_HIT_TEST_METRICS hitMetrics{};
                FLOAT hitX{};
                FLOAT hitY{};
                if (SUCCEEDED(layout->HitTestTextPosition(
                    static_cast<UINT32>(m_scrollColumn), FALSE, &hitX, &hitY, &hitMetrics)))
                {
                    dwriteScrollX = hitX;
                }
            }
            drawingSession.DrawText(hstring{ std::to_wstring(lineNumber) }, 8.0f, static_cast<float>(visibleLine * LineHeight) + 2.0f, LineNumberColor, format);
            auto const visibleText = m_scrollColumn < line.size() ? line.substr(m_scrollColumn) : L"";
            auto drawX = GutterWidth + 10.0f;
            std::size_t tokenStart = 0;
            while (tokenStart < visibleText.size())
            {
                auto tokenEnd = tokenStart + 1;
                Windows::UI::Color color = TextColor;
                if (visibleText[tokenStart] == L'/' && tokenEnd < visibleText.size() && visibleText[tokenEnd] == L'/')
                {
                    tokenEnd = visibleText.size();
                    color = CommentColor;
                }
                else if (visibleText[tokenStart] == L'\"')
                {
                    while (tokenEnd < visibleText.size() && visibleText[tokenEnd] != L'\"')
                    {
                        ++tokenEnd;
                    }
                    if (tokenEnd < visibleText.size())
                    {
                        ++tokenEnd;
                    }
                    color = StringColor;
                }
                else if (std::iswalpha(visibleText[tokenStart]) || visibleText[tokenStart] == L'_')
                {
                    while (tokenEnd < visibleText.size()
                        && (std::iswalnum(visibleText[tokenEnd]) || visibleText[tokenEnd] == L'_'
                            || visibleText[tokenEnd] == L':'))
                    {
                        ++tokenEnd;
                    }
                    if (IsKeyword(std::wstring_view{ visibleText }.substr(tokenStart, tokenEnd - tokenStart)))
                    {
                        color = KeywordColor;
                    }
                }
                else if (std::iswdigit(visibleText[tokenStart]))
                {
                    while (tokenEnd < visibleText.size()
                        && (std::iswalnum(visibleText[tokenEnd]) || visibleText[tokenEnd] == L'.'))
                    {
                        ++tokenEnd;
                    }
                    color = NumberColor;
                }

                auto const absoluteColumn = m_scrollColumn + tokenStart;
                for (auto const& semantic : m_semanticTokens)
                {
                    if (semantic.Line + 1 != lineNumber
                        || absoluteColumn < semantic.Column
                        || absoluteColumn >= semantic.Column + semantic.Length)
                    {
                        continue;
                    }
                    switch (semantic.Type)
                    {
                    case 1: color = Windows::UI::Color{ 0xFF, 0x4E, 0xC9, 0xB0 }; break;
                    case 2: color = Windows::UI::Color{ 0xFF, 0x4E, 0xC9, 0xB0 }; break;
                    case 8: color = Windows::UI::Color{ 0xFF, 0x9C, 0xDC, 0xFE }; break;
                    case 13: color = Windows::UI::Color{ 0xFF, 0xDC, 0xDC, 0xAA }; break;
                    case 15: color = KeywordColor; break;
                    case 17: color = CommentColor; break;
                    case 18: color = StringColor; break;
                    case 19: color = NumberColor; break;
                    case 20: color = Windows::UI::Color{ 0xFF, 0xD4, 0xD4, 0xD4 }; break;
                    default: break;
                    }
                    break;
                }

                auto const token = visibleText.substr(tokenStart, tokenEnd - tokenStart);
                auto tokenX = drawX;
                if (layout)
                {
                    DWRITE_HIT_TEST_METRICS hitMetrics{};
                    FLOAT hitX{};
                    FLOAT hitY{};
                    if (SUCCEEDED(layout->HitTestTextPosition(
                        static_cast<UINT32>(m_scrollColumn + tokenStart), FALSE, &hitX, &hitY, &hitMetrics)))
                    {
                        tokenX = GutterWidth + 10.0f + hitX - dwriteScrollX;
                    }
                }
                drawingSession.DrawText(
                    hstring{ token },
                    tokenX,
                    static_cast<float>(visibleLine * LineHeight) + 2.0f,
                    color,
                    format);
                drawX = tokenX + static_cast<float>(token.size()) * CharacterWidth;
                tokenStart = tokenEnd;
            }

            if (m_selectionLength == 0 && m_selectionStart >= static_cast<int32_t>(lineStart) && m_selectionStart <= static_cast<int32_t>(end))
            {
                auto const caretColumn = static_cast<std::size_t>(m_selectionStart) - lineStart;
                if (caretColumn >= m_scrollColumn)
                {
                    auto const caretX = GutterWidth + 10.0f + static_cast<float>(caretColumn - m_scrollColumn) * CharacterWidth;
                    drawingSession.FillRectangle(caretX, static_cast<float>(visibleLine * LineHeight), 1.5f, LineHeight, CaretColor);
                }
            }

            ++lineNumber;
            if (lineEnd == std::wstring::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }
    }

    void EditorDocumentControl::RaiseTextChanged()
    {
        if (m_textChanged)
        {
            m_textChanged(*this, nullptr);
        }
    }

    void EditorDocumentControl::RaiseSelectionChanged()
    {
        if (m_selectionChanged)
        {
            m_selectionChanged(*this, nullptr);
        }
    }

    void EditorDocumentControl::RaiseBreakpointRequested(int32_t line)
    {
        if (m_breakpointRequested)
        {
            m_breakpointRequested(*this, line);
        }
    }
}
