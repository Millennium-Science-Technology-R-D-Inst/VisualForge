#pragma once

#include <cstddef>
#include <vector>

namespace VisualForge::EditorCore::Cursor
{
    struct Cursor
    {
        std::size_t offset{ 0 };
    };

    class CursorManager final
    {
    public:
        void SetPrimaryCursor(std::size_t offset);
        void AddCursor(std::size_t offset);
        void ClearSecondaryCursors();
        [[nodiscard]] std::vector<Cursor> GetCursors() const;
        [[nodiscard]] Cursor PrimaryCursor() const noexcept;

    private:
        std::vector<Cursor> m_cursors{ Cursor{} };
    };
}
