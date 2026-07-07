#include "pch.h"

#include "EditorCore/LSP/LspProtocol.h"

namespace VisualForge::EditorCore::LSP
{
    LspProtocolMessage LspProtocolParser::Parse(JsonRpcMessage const& message)
    {
        auto summary = Protocol::JsonMessageSummaryParser::Parse(message.payload);
        auto diagnostics = summary.Method == L"textDocument/publishDiagnostics"
            ? ParseDiagnostics(message.payload)
            : std::vector<LspDiagnostic>{};

        return { std::move(summary), std::move(diagnostics), message.payload };
    }

    std::vector<LspDiagnostic> LspProtocolParser::ParseDiagnostics(std::string_view payload)
    {
        std::vector<LspDiagnostic> diagnostics;
        auto uri = Protocol::JsonMessageSummaryParser::ReadStringField(payload, "uri").value_or(L"");

        std::size_t searchOffset = 0;
        while (searchOffset < payload.size())
        {
            auto const messageField = payload.find("\"message\"", searchOffset);
            if (messageField == std::string_view::npos)
            {
                break;
            }

            auto const message = Protocol::JsonMessageSummaryParser::ReadStringField(payload.substr(messageField), "message").value_or(L"");
            auto const severityValue = Protocol::JsonMessageSummaryParser::ReadIntField(payload.substr(searchOffset, messageField - searchOffset), "severity").value_or(0);
            diagnostics.push_back({
                uri,
                message,
                static_cast<LspDiagnosticSeverity>(severityValue)
            });

            searchOffset = messageField + 9;
        }

        return diagnostics;
    }
}
