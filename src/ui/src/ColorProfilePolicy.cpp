#include "slideio/viewer/ui/ColorProfilePolicy.h"

#include "slideio/viewer/core/ColorManagement.h"

#include <QDir>
#include <QFile>
#include <QString>

namespace slideio::viewer::ui
{

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
        outProblem = core::defaultProfileProblemText(
            status, QDir::toNativeSeparators(path).toStdString());
        return fallback;
    }

    core::SuppliedColorProfile supplied;
    supplied.bytes = std::move(bytes);
    supplied.isSlideOverride = true;
    return supplied;
}

} // namespace slideio::viewer::ui
