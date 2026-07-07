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
        std::wstring Message;
        LspDiagnosticSeverity Severity{ LspDiagnosticSeverity::Unknown };
    };

    struct LspProtocolMessage
    {
        Protocol::JsonMessageSummary Summary;
        std::vector<LspDiagnostic> Diagnostics;
        std::string Payload;
    };

    class LspProtocolParser final
    {
    public:
        [[nodiscard]] static LspProtocolMessage Parse(JsonRpcMessage const& message);

    private:
        [[nodiscard]] static std::vector<LspDiagnostic> ParseDiagnostics(std::string_view payload);
    };
}
