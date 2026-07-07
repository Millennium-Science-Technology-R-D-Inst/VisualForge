#pragma once

#include "EditorCore/TextModel/TextBuffer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace VisualForge::EditorCore::Search
{
    struct SearchOptions
    {
        bool MatchCase{ false };
        bool WholeWord{ false };
    };

    struct SearchMatch
    {
        std::size_t Start{ 0 };
        std::size_t Length{ 0 };
    };

    class SearchEngine final
    {
    public:
        [[nodiscard]] static std::vector<SearchMatch> FindAll(
            TextModel::TextBuffer const& buffer,
            std::wstring_view query,
            SearchOptions options = {});

        [[nodiscard]] static std::size_t ReplaceAll(
            TextModel::TextBuffer& buffer,
            std::wstring_view query,
            std::wstring_view replacement,
            SearchOptions options = {});

    private:
        [[nodiscard]] static std::wstring Normalize(std::wstring_view text, bool matchCase);
        [[nodiscard]] static bool IsWholeWordMatch(std::wstring_view text, std::size_t start, std::size_t length);
        [[nodiscard]] static bool IsWordCharacter(wchar_t value) noexcept;
    };
}
