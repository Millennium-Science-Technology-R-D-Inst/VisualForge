#pragma once

#include "EditorCore/Cursor/CursorManager.h"
#include "EditorCore/Editing/EditHistory.h"
#include "EditorCore/Layout/LineLayoutEngine.h"
#include "EditorCore/LSP/LspClient.h"
#include "EditorCore/TextModel/TextBuffer.h"
#include "EditorCore/Viewport/ViewportController.h"

#include <filesystem>
#include <string>

namespace VisualForge::EditorCore::Document
{
    enum class TextEncoding
    {
        Utf8,
        Utf8Bom,
        Utf16Le,
        Utf16Be
    };

    class TextDocument final
    {
    public:
        TextDocument() = default;
        explicit TextDocument(std::filesystem::path path);

        void LoadText(std::wstring text);
        void SetEncoding(TextEncoding encoding) noexcept;
        void Save();
        void SaveAs(std::filesystem::path path);
        void Insert(std::size_t position, std::wstring text);
        void Delete(std::size_t position, std::size_t length);
        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();
        void Reflow();

        [[nodiscard]] std::filesystem::path const& Path() const noexcept;
        [[nodiscard]] TextModel::TextBuffer& Buffer() noexcept;
        [[nodiscard]] TextModel::TextBuffer const& Buffer() const noexcept;
        [[nodiscard]] LSP::LspClient& LanguageService() noexcept;
        [[nodiscard]] Layout::LineLayoutEngine& Layout() noexcept;
        [[nodiscard]] Viewport::ViewportController& Viewport() noexcept;
        [[nodiscard]] Cursor::CursorManager& Cursors() noexcept;
        [[nodiscard]] bool IsDirty() const noexcept;
        [[nodiscard]] bool CanUndo() const noexcept;
        [[nodiscard]] bool CanRedo() const noexcept;

    private:
        std::filesystem::path m_path;
        TextModel::TextBuffer m_buffer;
        LSP::LspClient m_lsp;
        Layout::LineLayoutEngine m_layout;
        Viewport::ViewportController m_viewport;
        Cursor::CursorManager m_cursors;
        Editing::EditHistory m_history;
        TextEncoding m_encoding{ TextEncoding::Utf8 };
        bool m_isDirty{ false };
    };
}
