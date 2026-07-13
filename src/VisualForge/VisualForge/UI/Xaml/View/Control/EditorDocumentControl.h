#pragma once

#include "UI/Xaml/View/Control/EditorDocumentControl.g.h"
#include "EditorCore/Rendering/DirectWriteContext.h"

#include <set>
#include <string>

namespace winrt::VisualForge::UI::Xaml::View::Control::implementation
{
    struct EditorDocumentControl : EditorDocumentControlT<EditorDocumentControl>
    {
        EditorDocumentControl();

        winrt::hstring Text() const;
        int32_t SelectionStart() const;
        int32_t SelectionLength() const;
        void SetText(winrt::hstring const& text);
        void ReplaceSelection(winrt::hstring const& text);
        void Select(int32_t start, int32_t length);
        void FocusEditor();
        void SetBreakpoint(int32_t line, bool enabled);
        void SetDebugLine(int32_t line);

        winrt::event_token TextChanged(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable> const& handler);
        void TextChanged(winrt::event_token const& token) noexcept;
        winrt::event_token SelectionChanged(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable> const& handler);
        void SelectionChanged(winrt::event_token const& token) noexcept;
        winrt::event_token CompletionRequested(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable> const& handler);
        void CompletionRequested(winrt::event_token const& token) noexcept;
        winrt::event_token BreakpointRequested(
            winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, int32_t> const& handler);
        void BreakpointRequested(winrt::event_token const& token) noexcept;
        void CompletionKeyboardAccelerator_Invoked(
            winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

        void EditorCanvas_Draw(
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl const& sender,
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasDrawEventArgs const& args);
        void EditorCanvas_PointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void InputTextBox_TextChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void InputTextBox_SelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EditorDocumentControl_PointerWheelChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
        void RaiseTextChanged();
        void RaiseSelectionChanged();
        void RaiseBreakpointRequested(int32_t line);
        void EnsureCaretVisible();
        [[nodiscard]] std::size_t LineCount() const noexcept;

        std::wstring m_text;
        int32_t m_selectionStart{ 0 };
        int32_t m_selectionLength{ 0 };
        std::size_t m_scrollLine{ 0 };
        std::size_t m_scrollColumn{ 0 };
        std::set<int32_t> m_breakpointLines;
        int32_t m_debugLine{ 0 };
        bool m_isLoading{ false };
        ::VisualForge::EditorCore::Rendering::DirectWriteContext m_dwrite;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable>> m_textChanged;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable>> m_selectionChanged;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, winrt::Windows::Foundation::IInspectable>> m_completionRequested;
        winrt::event<winrt::Windows::Foundation::TypedEventHandler<winrt::Windows::Foundation::IInspectable, int32_t>> m_breakpointRequested;
    };
}

namespace winrt::VisualForge::UI::Xaml::View::Control::factory_implementation
{
    struct EditorDocumentControl : EditorDocumentControlT<EditorDocumentControl, implementation::EditorDocumentControl>
    {
    };
}
