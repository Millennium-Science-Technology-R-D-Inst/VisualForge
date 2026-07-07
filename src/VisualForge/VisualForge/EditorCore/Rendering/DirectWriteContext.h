#pragma once

#include <dwrite_3.h>

#include <string>
#include <string_view>
#include <winrt/base.h>

namespace VisualForge::EditorCore::Rendering
{
    struct DirectWriteRenderMetrics
    {
        std::size_t GlyphRunCount{ 0 };
        std::size_t GlyphCount{ 0 };
        bool Succeeded{ false };
    };

    class DirectWriteContext final
    {
    public:
        [[nodiscard]] bool EnsureInitialized();
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] winrt::com_ptr<IDWriteTextLayout> CreateTextLayout(
            std::wstring_view text,
            float maxWidth,
            float maxHeight);
        [[nodiscard]] DirectWriteRenderMetrics DrawTextLayout(IDWriteTextLayout* layout);

    private:
        winrt::com_ptr<IDWriteFactory> m_factory;
        winrt::com_ptr<IDWriteTextFormat> m_textFormat;
        bool m_isInitialized{ false };
    };
}
