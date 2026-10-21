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

    /// The directory a stored (or entered) value resolves to, with the same
    /// rule workspaceDirectory() applies: anything empty after trimming falls
    /// back to the default.
    ///
    /// Static because MainWindow needs to resolve a value the user has typed
    /// but not yet committed -- writing it first would make it
    /// indistinguishable from a folder the save-failure resolver had already
    /// chosen. One implementation, so the two callers cannot drift: this rule
    /// decides where a pathologist's annotations are written.
    [[nodiscard]] static QString resolveWorkspaceDirectory(const QString& stored);

    /// The configured workspace, or defaultAnnotationWorkspaceDirectory() when
    /// nothing usable is stored. Never empty.
    ///
    /// THIS INSTANCE's workspace: read from the store once, at construction,
    /// and changed afterwards only by this object's setWorkspaceDirectory().
    /// The store is shared by every running instance while the repository
    /// root it mirrors is not, so re-reading it showed one instance the folder
    /// another had just chosen -- in Preferences, where an OK left unedited
    /// would then have switched it there, into the other instance's lock. The
    /// store is still written on every change, so the next launch reads
    /// whichever instance chose last.
    [[nodiscard]] QString workspaceDirectory() const;

    /// This instance's setting as stored, before resolveWorkspaceDirectory()
    /// fills in a default -- per instance, like workspaceDirectory(), so a
    /// Cancel restores what THIS instance had. Empty means "follow the default", which is NOT the same thing
    /// as the default path written out: a caller that restores the resolved
    /// path pins the workspace to today's Documents folder forever, so a user
    /// who later moves it keeps writing annotations to the old location.
    ///
    /// Only for callers that restore a previous value. Everything that wants
    /// to know where annotations go uses workspaceDirectory().
    [[nodiscard]] QString configuredWorkspaceDirectory() const;

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
    /// As stored, trimmed, before resolution; empty means "follow the default".
    /// See workspaceDirectory() for why this is not re-read from m_settings.
    QString m_workspace;
};

} // namespace slideio::viewer::ui
