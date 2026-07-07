#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace VisualForge::EditorCore::TextModel
{
    class TextSnapshot final
    {
    public:
        TextSnapshot() = default;
        explicit TextSnapshot(std::wstring text);

        [[nodiscard]] wchar_t CharAt(std::size_t offset) const;
        [[nodiscard]] std::wstring GetText(std::size_t start, std::size_t length) const;
        [[nodiscard]] std::wstring_view Text() const noexcept;
        [[nodiscard]] std::size_t Length() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;

    private:
        std::wstring m_text;
    };
}
