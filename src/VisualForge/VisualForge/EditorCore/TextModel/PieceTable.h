#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace VisualForge::EditorCore::TextModel
{
    class PieceTable final
    {
    public:
        PieceTable() = default;
        explicit PieceTable(std::wstring originalText);

        void Reset(std::wstring originalText);
        void Insert(std::size_t position, std::wstring_view text);
        [[nodiscard]] std::wstring Delete(std::size_t position, std::size_t length);

        [[nodiscard]] std::wstring GetText(std::size_t start, std::size_t length) const;
        [[nodiscard]] std::wstring GetAllText() const;
        [[nodiscard]] wchar_t CharAt(std::size_t position) const;
        [[nodiscard]] std::size_t Length() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;

    private:
        enum class BufferKind
        {
            Original,
            Add
        };

        struct Piece
        {
            BufferKind buffer{ BufferKind::Original };
            std::size_t start{ 0 };
            std::size_t length{ 0 };
        };

        struct PieceLocation
        {
            std::size_t pieceIndex{ 0 };
            std::size_t innerOffset{ 0 };
        };

        [[nodiscard]] PieceLocation Locate(std::size_t position) const;
        [[nodiscard]] std::wstring_view BufferFor(Piece const& piece) const noexcept;
        void Normalize();

        std::vector<Piece> m_pieces;
        std::wstring m_original;
        std::wstring m_addBuffer;
        std::size_t m_length{ 0 };
    };
}
