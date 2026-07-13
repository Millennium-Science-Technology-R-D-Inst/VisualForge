#pragma once

#include "EditorCore/Document/TextDocument.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace VisualForge::EditorCore::Document
{
    class DocumentManager final
    {
    public:
        TextDocument& Open(std::filesystem::path path);
        TextDocument& NewUntitled(std::wstring initialText = {});
        bool Save(std::filesystem::path const& path);
        bool SaveAs(std::filesystem::path const& oldPath, std::filesystem::path newPath);
        bool Close(std::filesystem::path const& path);
        [[nodiscard]] TextDocument* Find(std::filesystem::path const& path);
        [[nodiscard]] std::size_t Count() const noexcept;

    private:
        std::unordered_map<std::wstring, TextDocument> m_documents;
        std::size_t m_nextUntitledId{ 1 };
    };
}
