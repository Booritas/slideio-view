#include "slideio/viewer/ui/PathDisplay.h"

namespace slideio::viewer::ui
{

namespace
{

bool isSeparator(QChar c)
{
    return c == QLatin1Char('/') || c == QLatin1Char('\\');
}

} // namespace

QString slideDisplayName(const QString& path)
{
    // Deliberately string-based rather than QFileInfo-based: this runs for
    // paths that may not exist (a stale recent-files entry, a command-line
    // argument) and must not touch the filesystem to produce a label.
    int end = path.size();
    while (end > 0 && isSeparator(path.at(end - 1))) {
        --end;
    }

    int start = end;
    while (start > 0 && !isSeparator(path.at(start - 1))) {
        --start;
    }

    const QString name = path.mid(start, end - start);

    // A drive root ("D:/") and a bare separator reduce to nothing useful;
    // showing the path itself beats showing an empty label.
    return name.isEmpty() || name.endsWith(QLatin1Char(':')) ? path : name;
}

} // namespace slideio::viewer::ui
