#pragma once

#include "EditorCore/LSP/JsonRpc.h"
#include "EditorCore/Protocol/JsonMessageSummary.h"

#include <string>
#include <vector>

namespace VisualForge::EditorCore::LSP
{
    enum class LspDiagnosticSeverity
    {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4,
        Unknown = 0
    };

    struct LspDiagnostic
    {
        std::wstring Uri;
        std::size_t Line{};
        std::size_t Column{};
        std::size_t EndLine{};
        std::size_t EndColumn{};
        std::wstring Code;
        std::wstring Message;
        LspDiagnosticSeverity Severity{ LspDiagnosticSeverity::Unknown };
    };

    struct LspCompletionItem
    {
        std::wstring Label;
        std::wstring Detail;
        std::wstring InsertText;
    };

    struct LspLocation
    {
        std::wstring Uri;
        std::size_t Line{};
        std::size_t Column{};
        std::size_t EndLine{};
        std::size_t EndColumn{};
    };

    struct LspTextEdit
    {
        LspLocation Location;
        std::wstring NewText;
    };

    struct LspCodeAction
    {
        std::wstring Title;
        std::vector<LspTextEdit> TextEdits;
    };

    struct LspDocumentSymbol
    {
        std::wstring Name;
        std::wstring Detail;
        std::size_t Kind{};
        LspLocation Location;
    };

    struct LspSemanticToken
    {
        std::size_t Line{};
        std::size_t Column{};
        std::size_t Length{};
        std::size_t TokenType{};
    };

    struct LspProtocolMessage
    {
        Protocol::JsonMessageSummary Summary;
        std::vector<LspDiagnostic> Diagnostics;
        std::vector<LspCompletionItem> Completions;
        std::wstring HoverText;
        std::vector<LspLocation> Locations;
        std::vector<LspTextEdit> TextEdits;
        std::vector<LspCodeAction> CodeActions;
        std::vector<LspDocumentSymbol> DocumentSymbols;
        std::vector<LspSemanticToken> SemanticTokens;
        std::string Payload;
    };

    class LspProtocolParser final
    {
    public:
        [[nodiscard]] static LspProtocolMessage Parse(JsonRpcMessage const& message);

    private:
        [[nodiscard]] static std::vector<LspDiagnostic> ParseDiagnostics(std::string_view payload);
        [[nodiscard]] static std::vector<LspCompletionItem> ParseCompletions(std::string_view payload);
        [[nodiscard]] static std::wstring ParseHover(std::string_view payload);
        [[nodiscard]] static std::vector<LspLocation> ParseLocations(std::string_view payload);
        [[nodiscard]] static std::vector<LspTextEdit> ParseWorkspaceEdits(std::string_view payload);
        [[nodiscard]] static std::vector<LspTextEdit> ParseDocumentEdits(std::string_view payload);
        [[nodiscard]] static std::vector<LspCodeAction> ParseCodeActions(std::string_view payload);
        [[nodiscard]] static std::vector<LspDocumentSymbol> ParseDocumentSymbols(std::string_view payload);
        [[nodiscard]] static std::vector<LspSemanticToken> ParseSemanticTokens(std::string_view payload);
    };
}
