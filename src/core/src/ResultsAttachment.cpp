#include "slideio/viewer/core/ResultsAttachment.h"

#include <filesystem>

namespace slideio::viewer::core
{

namespace
{

/// `text` is UTF-8. Built through u8path, never the narrow constructor: on
/// Windows that reads the ANSI code page, which mangles a non-ASCII path and
/// can throw on a DBCS one. May throw (allocation, conversion).
std::string canonical(const std::string& text)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path given = fs::u8path(text);
    fs::path p = fs::absolute(given, ec);
    if (ec) {
        p = given;
    }
    p = p.lexically_normal();
    std::string out = p.generic_u8string();
    // "a/b/" names the same file as "a/b"; a root ("/", "C:/") keeps its slash.
    if (p.has_relative_path() && out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
#ifdef _WIN32
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        } else if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
#endif
    return out;
}

} // namespace

bool sameSlidePath(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) {
        return false;
    }
    // Called from Qt slots: nothing may escape.
    try {
        return canonical(a) == canonical(b);
    } catch (...) {
        return false;
    }
}

AttachmentDecision decideAttachment(const ResultsProvenance& workspace, const std::string& openSlidePath,
                                    int openScene, const std::string& attachAnywaySlidePath)
{
    if (workspace.scene != openScene) {
        return AttachmentDecision::RefuseScene;
    }
    if (sameSlidePath(workspace.slidePath, openSlidePath) ||
        sameSlidePath(attachAnywaySlidePath, openSlidePath)) {
        return AttachmentDecision::Attach;
    }
    return AttachmentDecision::WarnSlideMoved;
}

} // namespace slideio::viewer::core
