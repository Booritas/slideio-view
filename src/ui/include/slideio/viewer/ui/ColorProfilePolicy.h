#pragma once

#include "slideio/viewer/core/ColorProfileOverride.h"
#include "slideio/viewer/core/SlideId.h"
#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slideio::viewer::ui
{

/// Everything the open path needs to choose a colour profile, snapshotted on
/// the UI thread and captured by value into the background open threads.
///
/// It holds no QSettings handle and no pointer back to MainWindow on purpose:
/// resolution runs on the open thread, where neither may be touched.
///
/// Paths rather than bytes, so the snapshot stays small when a user has many
/// overrides, and so a profile edited on disk takes effect at the next slide
/// open rather than at the next restart.
struct ColorProfilePolicy
{
    std::vector<uint8_t> defaultBytes; ///< already validated; empty if none
    std::unordered_map<std::string, std::string> overridePathsBySlideId;
};

/// The byte size of a slide's content, for use as an identity input.
///
/// A slide is not always one file: a DICOM study is a directory of .dcm files.
/// std::filesystem::file_size does not report a directory's contents -- on MSVC
/// it does not even fail, it returns the directory entry's own size, the same
/// small constant for every study. Summing the regular files beneath it keeps
/// the size discriminating for exactly the family of slides that would
/// otherwise all size alike: two studies from one scanner share their view
/// count, detector dimensions and channel count, so the byte count is the only
/// thing left separating them.
///
/// nullopt means the size could not be determined at all. Callers treat that as
/// "cannot identify this slide", never as size zero.
std::optional<uint64_t> slideContentSize(const std::string& path);

/// Choose the profile for a slide, given its scene table and file size.
///
/// Precedence: a per-slide override whose file validates now, else the default.
/// (A slide's embedded profile outranks the default but not an override; that
/// part of the precedence lives in buildManagedScene, which is the only place
/// that knows what the slide embeds.)
///
/// An empty `scenes` is never matched against: enumeration failed, every such
/// slide hashes alike, and matching would apply one slide's override to
/// another.
///
/// `outProblem` is set, and the default returned, when an override is
/// configured but unusable -- so the user is told their setting is at fault
/// rather than being shown a message blaming the slide.
core::SuppliedColorProfile resolveColorProfile(const ColorProfilePolicy& policy,
                                               const std::vector<core::SceneInfo>& scenes,
                                               uint64_t fileSizeBytes,
                                               std::string& outProblem);

} // namespace slideio::viewer::ui
