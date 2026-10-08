#include "slideio/viewer/ui/ColorProfilePolicy.h"

#include "slideio/viewer/core/ColorManagement.h"

#include <QDir>
#include <QFile>
#include <QString>

#include <filesystem>
#include <system_error>

namespace slideio::viewer::ui
{

std::optional<uint64_t> slideContentSize(const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path p(path);

    // Checked before file_size rather than after it fails, because on MSVC it
    // does not fail on a directory -- it succeeds and returns the directory
    // entry's own size.
    const bool isDir = std::filesystem::is_directory(p, ec);
    if (ec) {
        return std::nullopt;
    }

    if (isDir) {
        uint64_t total = 0;
        for (std::filesystem::recursive_directory_iterator it(p, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                return std::nullopt;
            }
            if (it->is_regular_file(ec)) {
                const auto size = it->file_size(ec);
                if (ec) {
                    return std::nullopt;
                }
                total += static_cast<uint64_t>(size);
            }
            if (ec) {
                return std::nullopt;
            }
        }
        if (ec) {
            return std::nullopt;
        }
        return total;
    }

    const auto size = std::filesystem::file_size(p, ec);
    if (ec) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(size);
}

core::SuppliedColorProfile resolveColorProfile(const ColorProfilePolicy& policy,
                                               const std::vector<core::SceneInfo>& scenes,
                                               uint64_t fileSizeBytes,
                                               std::string& outProblem)
{
    outProblem.clear();

    core::SuppliedColorProfile fallback;
    fallback.bytes = policy.defaultBytes;
    fallback.isSlideOverride = false;

    // Enumeration failed. The identity of an unreadable file is the identity of
    // every unreadable file, so looking it up could hand this slide an override
    // belonging to a different one.
    if (scenes.empty()) {
        return fallback;
    }

    const std::string slideId = core::computeSlideId(scenes, fileSizeBytes);
    const auto it = policy.overridePathsBySlideId.find(slideId);
    if (it == policy.overridePathsBySlideId.end()) {
        return fallback;
    }

    const QString path = QString::fromStdString(it->second);
    QFile file(path);
    const bool readable = file.open(QIODevice::ReadOnly);

    std::vector<uint8_t> bytes;
    if (readable) {
        const QByteArray raw = file.readAll();
        bytes.assign(raw.begin(), raw.end());
    }

    // Revalidated on every open, not only when the user picked the file: the
    // setting stores a path, so the file can be truncated, replaced or removed
    // afterwards.
    const core::DefaultProfileStatus status =
        core::classifyDefaultProfile(readable, core::inspectIccHeader(bytes));
    if (status != core::DefaultProfileStatus::Ok) {
        // The per-slide wording, not defaultProfileProblemText's: what failed
        // here is the override the user set for this one slide, and it is
        // repaired in the manage dialog, not in the default-profile setting.
        outProblem = core::slideProfileProblemText(
            status, QDir::toNativeSeparators(path).toStdString());
        return fallback;
    }

    core::SuppliedColorProfile supplied;
    supplied.bytes = std::move(bytes);
    supplied.isSlideOverride = true;
    return supplied;
}

core::SuppliedColorProfile resolveColorProfileForOpen(const ColorProfilePolicy& policy,
                                                      const std::vector<core::SceneInfo>& scenes,
                                                      uint64_t fileSizeBytes,
                                                      std::string& outProblem)
{
    const core::SuppliedColorProfile supplied =
        resolveColorProfile(policy, scenes, fileSizeBytes, outProblem);
    // Reported only when the policy actually holds an override that could have
    // been dropped. With none configured there is nothing to warn about, and a
    // DICOM study whose size could not be taken would otherwise tell a user who
    // has never set a per-slide profile that one of theirs was not applied.
    //
    // The map being non-empty is the right granularity: if the user has
    // overrides for other slides and this one cannot be identified, we genuinely
    // cannot tell whether one of them was this slide's, so the message belongs.
    if (scenes.empty() && !policy.overridePathsBySlideId.empty()) {
        // resolveColorProfile declined the lookup and left outProblem empty --
        // correctly, since passing it an empty vector is indistinguishable from
        // "enumeration legitimately found nothing" from inside that function.
        // Only the caller knows this one means "could not identify the slide",
        // so only the caller can say so.
        outProblem = "This slide could not be identified, so any color profile "
                     "saved for it was not applied.";
    }
    return supplied;
}

std::string clearedSlideProfileMessage()
{
    return "Cleared the color profile for this slide.";
}

std::string clearedDefaultProfileMessage(bool slideOpen)
{
    std::string message = "Cleared the default color profile.";
    if (slideOpen) {
        message += " Reopen the current slide for the change to affect it.";
    }
    return message;
}

} // namespace slideio::viewer::ui
