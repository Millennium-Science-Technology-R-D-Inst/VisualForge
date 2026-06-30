#include "pch.h"

#include "Tool/ToolCommand.h"

#include <cwctype>

namespace VisualForge::Tool
{
    std::wstring QuoteArgument(std::wstring_view argument)
    {
        if (argument.empty())
        {
            return L"\"\"";
        }

        bool needsQuotes = false;
        for (wchar_t value : argument)
        {
            if (std::iswspace(value) || value == L'"')
            {
                needsQuotes = true;
                break;
            }
        }

        if (!needsQuotes)
        {
            return std::wstring{ argument };
        }

        std::wstring quoted;
        quoted.reserve(argument.size() + 2);
        quoted.push_back(L'"');

        for (wchar_t value : argument)
        {
            if (value == L'"')
            {
                quoted.append(L"\\\"");
            }
            else
            {
                quoted.push_back(value);
            }
        }

        quoted.push_back(L'"');
        return quoted;
    }

    std::wstring ToolCommand::ToCommandLine() const
    {
        std::wstring commandLine = QuoteArgument(Executable.empty() ? ToExecutableName(Kind) : Executable);

        for (auto const& argument : Arguments)
        {
            commandLine.push_back(L' ');
            commandLine.append(QuoteArgument(argument));
        }

        return commandLine;
    }
}
