#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace VisualForge::EditorCore::Layout
{
    struct CachedGlyphRun
    {
        std::wstring text;
        float width{ 0.0f };
        float height{ 18.0f };
    };

    class GlyphRunCache final
    {
    public:
        void Put(std::size_t lineIndex, CachedGlyphRun run);
        [[nodiscard]] CachedGlyphRun const* Find(std::size_t lineIndex) const noexcept;
        void Invalidate(std::size_t startLine, std::size_t endLine);
        void Clear();

    private:
        std::unordered_map<std::size_t, CachedGlyphRun> m_runs;
    };
}
