#pragma once

#include <QString>
#include <QStringList>

namespace slideio::viewer::ui
{

/// Returns `path` in the one spelling the recent-files list stores.
///
/// The same slide reaches the application in more than one spelling: the file
/// dialog and a drop give forward slashes, the shell's command line
/// (`main.cpp`, "open with", a path typed into a terminal) gives backslashes,
/// and a DICOM study folder arrives with a trailing separator. The menu labels
/// an entry with its last component alone, so two spellings of one slide read
/// as that slide listed twice -- which is what group AC saw.
///
/// Deliberately string-only, like `slideDisplayName`: a recent-files entry may
/// name a file that has since moved, and resolving it against the filesystem
/// would silently drop the entry instead of listing it.
[[nodiscard]] QString normalizedSlidePath(const QString& path);

/// Returns the recent-files list with `path` promoted to the front, every other
/// spelling of the same file removed, and the result truncated to
/// `maxEntries`. Entries already stored are normalized on the way through, so a
/// list that already holds two spellings of one slide is repaired the next time
/// it is written -- there is no separate migration step. An empty `path`
/// promotes nothing and performs that repair alone.
///
/// Two paths name one slide when they normalize alike; on Windows the
/// comparison also ignores case, because the filesystem does. On a
/// case-sensitive platform it must not, or a stored path would be rewritten
/// into one that no longer opens.
[[nodiscard]] QStringList recentFilesWithPathPromoted(const QStringList& files,
                                                      const QString& path,
                                                      int maxEntries);

} // namespace slideio::viewer::ui
