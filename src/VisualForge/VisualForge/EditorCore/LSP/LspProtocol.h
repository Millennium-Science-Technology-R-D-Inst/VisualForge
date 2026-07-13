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

    struct LspProtocolMessage
    {
        Protocol::JsonMessageSummary Summary;
        std::vector<LspDiagnostic> Diagnostics;
        std::vector<LspCompletionItem> Completions;
        std::wstring HoverText;
        std::vector<LspLocation> Locations;
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
    };
}
