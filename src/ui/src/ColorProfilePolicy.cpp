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
