#pragma once

#include "EditorCore/TextModel/PieceTable.h"
#include "EditorCore/TextModel/TextSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace VisualForge::EditorCore::TextModel
{
    class TextBuffer final
    {
    public:
        TextBuffer() = default;
        explicit TextBuffer(std::wstring text);

        void Load(std::wstring text);
        void Insert(std::size_t position, std::wstring_view text);
        [[nodiscard]] std::wstring Delete(std::size_t position, std::size_t length);

        [[nodiscard]] wchar_t CharAt(std::size_t position) const;
        [[nodiscard]] std::wstring GetText(std::size_t start, std::size_t length) const;
        [[nodiscard]] std::size_t Length() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] std::uint64_t Version() const noexcept;
        [[nodiscard]] TextSnapshot CreateSnapshot() const;

    private:
        PieceTable m_pieceTable;
        std::uint64_t m_version{ 0 };
    };
}
