#pragma once

#include "EditorCore/LSP/JsonRpc.h"
#include "EditorCore/Protocol/JsonMessageSummary.h"

#include <string>

namespace VisualForge::EditorCore::DAP
{
    enum class DapMessageKind
    {
        Unknown,
        Response,
        Event,
        Request
    };

    struct DapStackFrame
    {
        int Id{};
        std::wstring Name;
        std::wstring SourcePath;
        int Line{};
        int Column{};
    };

    struct DapScope
    {
        std::wstring Name;
        int VariablesReference{};
    };

    struct DapVariable
    {
        std::wstring Name;
        std::wstring Value;
        std::wstring Type;
        int VariablesReference{};
    };

    struct DapProtocolMessage
    {
        DapMessageKind Kind{ DapMessageKind::Unknown };
        Protocol::JsonMessageSummary Summary;
        int ThreadId{};
        std::vector<DapStackFrame> StackFrames;
        std::vector<DapScope> Scopes;
        std::vector<DapVariable> Variables;
        std::string Payload;
    };

    class DapProtocolParser final
    {
    public:
        [[nodiscard]] static DapProtocolMessage Parse(LSP::JsonRpcMessage const& message);

    private:
        [[nodiscard]] static std::vector<DapStackFrame> ParseStackFrames(std::string_view payload);
        [[nodiscard]] static std::vector<DapScope> ParseScopes(std::string_view payload);
        [[nodiscard]] static std::vector<DapVariable> ParseVariables(std::string_view payload);
    };
}
