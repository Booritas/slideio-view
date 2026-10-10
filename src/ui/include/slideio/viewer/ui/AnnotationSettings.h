#pragma once

#include <QString>

#include <memory>

class QSettings;

namespace slideio::viewer::ui
{

/// The two preferences annotation persistence needs: where files go, and whose
/// name goes on them.
///
/// Separate from PreferencesDialog so the resolution rules are testable -- no
/// test suite creates a QApplication, so no test can construct the dialog.
class AnnotationSettings
{
public:
    /// Uses the application's own QSettings store.
    AnnotationSettings();
    /// Uses an explicit INI file. For tests.
    explicit AnnotationSettings(const QString& iniFilePath);
    ~AnnotationSettings();

    AnnotationSettings(const AnnotationSettings&) = delete;
    AnnotationSettings& operator=(const AnnotationSettings&) = delete;

    /// The configured workspace, or defaultAnnotationWorkspaceDirectory() when
    /// nothing usable is stored. Never empty.
    [[nodiscard]] QString workspaceDirectory() const;

    /// An empty or whitespace-only path clears the setting rather than storing
    /// one that resolves to nothing.
    void setWorkspaceDirectory(const QString& path);

    /// Stamped into every annotation created from now on. Self-asserted and
    /// unverified: it identifies, it does not authenticate.
    [[nodiscard]] QString userName() const;
    void setUserName(const QString& name);

    /// FR-USER-01: annotation creation is blocked until this is true.
    [[nodiscard]] bool hasUserName() const;

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::ui
