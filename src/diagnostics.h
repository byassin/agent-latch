#pragma once

#include <string>
#include <string_view>

namespace agent_latch {

std::wstring FormatDiagnosticRecord(
    std::wstring_view timestamp,
    std::wstring_view event_name,
    std::wstring_view detail);

class DiagnosticsLog {
public:
    DiagnosticsLog();

    bool Write(std::wstring_view event_name, std::wstring_view detail = {});
    const std::wstring& Path() const;

private:
    bool RotateIfNeeded(std::size_t incoming_bytes) const;

    std::wstring directory_;
    std::wstring path_;
    std::wstring previous_path_;
};

}  // namespace agent_latch
