#pragma once

#include <string>

namespace slideio::viewer::core
{

/// What is true at the moment the annotation workspace is about to change.
/// Gathered by app::WorkspaceSwitch after it has flushed to the old folder and
/// probed the new one, so every number here is measured rather than predicted.
struct WorkspaceSwitchFacts
{
    /// Annotations on screen, which is also what was just flushed to oldPath.
    int currentCount = 0;
    /// False when the open annotations are read-only: a slide-id mismatch
    /// answered ReadOnly, an unidentifiable slide, or an associated image.
    bool currentWritable = true;
    /// The flush to the old folder failed. Reported to the user, never used to
    /// withhold an option -- the user may be switching away because of it.
    bool flushFailed = false;
    std::string oldPath;

    bool newFileExists = false;
    /// False when the new folder's file is malformed, unreadable, or written by
    /// a newer schema version.
    bool newFileReadable = true;
    int newCount = 0;
    std::string newPath;
};

/// Which outcomes may be offered. Cancel is always available and is not listed.
///
/// `loadNew` means "use whatever the new folder has", which for an empty new
/// folder means switching to an empty set -- the dialog words it accordingly.
struct WorkspaceSwitchOptions
{
    /// False means no dialog at all: switch without asking.
    bool ask = false;
    bool copy = false;
    bool keepCurrent = false;
    bool merge = false;
    bool loadNew = false;
};

/// The matrix in the spec, as a function. Pure, so the decision can be tested
/// where the dialog that renders it cannot be.
///
/// Two rules carry the weight. Nothing on screen means nothing is at risk, so
/// there is nothing to ask about. And a file that could not be parsed is never
/// a file we write to, so copy, keep-current and merge are all withheld -- each
/// of them would write over contents nobody has read.
[[nodiscard]] WorkspaceSwitchOptions workspaceSwitchOptions(const WorkspaceSwitchFacts& facts);

} // namespace slideio::viewer::core
