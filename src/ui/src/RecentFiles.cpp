#include "slideio/viewer/ui/RecentFiles.h"

#include <QDir>
#include <QtGlobal>

#include <algorithm>

namespace slideio::viewer::ui
{

namespace
{

bool sameSlidePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity kSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kSensitivity = Qt::CaseSensitive;
#endif
    return QString::compare(left, right, kSensitivity) == 0;
}

} // namespace

QString normalizedSlidePath(const QString& path)
{
    if (path.isEmpty()) {
        return path;
    }
    // cleanPath converts separators to '/' only where the platform's own
    // separator is not '/' already, which is exactly the rule this function
    // promises: a backslash is a separator on Windows and a file name
    // character everywhere else.
    return QDir::cleanPath(path);
}

QStringList recentFilesWithPathPromoted(const QStringList& files,
                                        const QString& path,
                                        int maxEntries)
{
    QStringList result;

    const QString promoted = normalizedSlidePath(path);
    if (!promoted.isEmpty()) {
        result.append(promoted);
    }

    for (const QString& entry : files) {
        const QString candidate = normalizedSlidePath(entry);
        if (candidate.isEmpty()) {
            continue;
        }
        // Compared against what has been kept rather than against the incoming
        // list, so a stored list that already holds two spellings of one slide
        // collapses to the first of them.
        const bool alreadyKept = std::any_of(result.cbegin(), result.cend(),
            [&candidate](const QString& kept) { return sameSlidePath(kept, candidate); });
        if (!alreadyKept) {
            result.append(candidate);
        }
    }

    while (result.size() > qMax(0, maxEntries)) {
        result.removeLast();
    }
    return result;
}

} // namespace slideio::viewer::ui
