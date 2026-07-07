#pragma once

#include "EditorCore/Document/TextDocument.h"

#include <string_view>

namespace VisualForge::EditorCore::Input
{
    enum class EditorNavigationUnit
    {
        Character,
        LineBoundary,
        DocumentBoundary
    };

    enum class EditorInputCommand
    {
        InsertText,
        Backspace,
        DeleteForward,
        MoveLeft,
        MoveRight,
        MoveToDocumentStart,
        MoveToDocumentEnd,
        Undo,
        Redo
    };

    class EditorInputController final
    {
    public:
        [[nodiscard]] bool InsertText(Document::TextDocument& document, std::wstring_view text);
        [[nodiscard]] bool Backspace(Document::TextDocument& document);
        [[nodiscard]] bool DeleteForward(Document::TextDocument& document);
        [[nodiscard]] bool MoveLeft(Document::TextDocument& document);
        [[nodiscard]] bool MoveRight(Document::TextDocument& document);
        [[nodiscard]] bool MoveToDocumentStart(Document::TextDocument& document);
        [[nodiscard]] bool MoveToDocumentEnd(Document::TextDocument& document);
        [[nodiscard]] bool Undo(Document::TextDocument& document);
        [[nodiscard]] bool Redo(Document::TextDocument& document);
    };
}
