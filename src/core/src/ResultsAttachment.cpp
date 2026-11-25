#include "slideio/viewer/core/ResultsAttachment.h"

#include <filesystem>

namespace slideio::viewer::core
{

namespace
{

std::string canonical(const std::string& text)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(text), ec);
    if (ec) {
        p = fs::path(text);
    }
    std::string out = p.lexically_normal().generic_string();
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
    return canonical(a) == canonical(b);
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
