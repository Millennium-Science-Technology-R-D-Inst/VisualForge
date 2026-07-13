#include "pch.h"

#include "EditorDocumentControl.h"

#if __has_include("UI/Xaml/View/Control/EditorDocumentControl.g.cpp")
#include "UI/Xaml/View/Control/EditorDocumentControl.g.cpp"
#endif

#include <algorithm>
#include <cwctype>
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

    void EditorDocumentControl::SetText(hstring const& text)
    {
        m_isLoading = true;
        m_text = text.c_str();
        InputTextBox().Text(text);
        InputTextBox().Select(0, 0);
        m_selectionStart = 0;
        m_selectionLength = 0;
        m_isLoading = false;
        m_scrollLine = 0;
        m_scrollColumn = 0;
        m_breakpointLines.clear();
        m_debugLine = 0;
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

        m_text.replace(start, (std::min)(length, m_text.size() - start), text.c_str());
        auto const next = start + text.size();
        SetText(hstring{ m_text });
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

    void EditorDocumentControl::InputTextBox_TextChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (m_isLoading)
        {
            return;
        }

        m_text = InputTextBox().Text().c_str();
        EnsureCaretVisible();
        EditorCanvas().Invalidate();
        RaiseTextChanged();
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
        EditorCanvas().Invalidate();
        args.Handled(true);
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
            (void)layout;
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

                auto const token = visibleText.substr(tokenStart, tokenEnd - tokenStart);
                drawingSession.DrawText(
                    hstring{ token },
                    drawX,
                    static_cast<float>(visibleLine * LineHeight) + 2.0f,
                    color,
                    format);
                drawX += static_cast<float>(token.size()) * CharacterWidth;
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
