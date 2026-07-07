#include "pch.h"

#include "EditorCore/Input/EditorInputController.h"

#include <algorithm>

namespace VisualForge::EditorCore::Input
{
    bool EditorInputController::InsertText(Document::TextDocument& document, std::wstring_view text)
    {
        if (text.empty())
        {
            return false;
        }

        auto const position = document.Cursors().PrimaryCursor().offset;
        document.Insert(position, std::wstring{ text });
        document.Cursors().SetPrimaryCursor(position + text.size());
        return true;
    }

    bool EditorInputController::Backspace(Document::TextDocument& document)
    {
        auto const position = document.Cursors().PrimaryCursor().offset;
        if (position == 0)
        {
            return false;
        }

        document.Delete(position - 1, 1);
        document.Cursors().SetPrimaryCursor(position - 1);
        return true;
    }

    bool EditorInputController::DeleteForward(Document::TextDocument& document)
    {
        auto const position = document.Cursors().PrimaryCursor().offset;
        if (position >= document.Buffer().Length())
        {
            return false;
        }

        document.Delete(position, 1);
        return true;
    }

    bool EditorInputController::MoveLeft(Document::TextDocument& document)
    {
        auto const position = document.Cursors().PrimaryCursor().offset;
        if (position == 0)
        {
            return false;
        }

        document.Cursors().SetPrimaryCursor(position - 1);
        return true;
    }

    bool EditorInputController::MoveRight(Document::TextDocument& document)
    {
        auto const position = document.Cursors().PrimaryCursor().offset;
        if (position >= document.Buffer().Length())
        {
            return false;
        }

        document.Cursors().SetPrimaryCursor(position + 1);
        return true;
    }

    bool EditorInputController::MoveToDocumentStart(Document::TextDocument& document)
    {
        document.Cursors().SetPrimaryCursor(0);
        return true;
    }

    bool EditorInputController::MoveToDocumentEnd(Document::TextDocument& document)
    {
        document.Cursors().SetPrimaryCursor(document.Buffer().Length());
        return true;
    }

    bool EditorInputController::Undo(Document::TextDocument& document)
    {
        return document.Undo();
    }

    bool EditorInputController::Redo(Document::TextDocument& document)
    {
        return document.Redo();
    }
}
