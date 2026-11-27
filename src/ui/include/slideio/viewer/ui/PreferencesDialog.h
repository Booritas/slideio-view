#pragma once

#include "slideio/viewer/ui/BuiltLevelCacheSettings.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace slideio::viewer::ui
{

/// The application's first preferences dialog: the annotation workspace and the
/// user name. Built to be extended; it holds only what E1 needs.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                      const QStringList& categories, const BuiltLevelCacheSettings& cacheSettings,
                      QWidget* parent = nullptr);

    [[nodiscard]] QString workspaceDirectory() const;
    [[nodiscard]] QString userName() const;
    [[nodiscard]] QStringList categories() const;
    /// The edited cache settings, the retention clamped to [1, 365] days and the
    /// folder trimmed (empty means the default).
    [[nodiscard]] BuiltLevelCacheSettings builtLevelCacheSettings() const;

private:
    void browseForWorkspace();
    void browseForCacheFolder();
    void updateCacheControls();

    QLineEdit* m_workspaceEdit = nullptr;
    QLineEdit* m_userNameEdit = nullptr;
    QPlainTextEdit* m_categoriesEdit = nullptr;
    QCheckBox* m_cacheEnabledBox = nullptr;
    QLineEdit* m_cacheFolderEdit = nullptr;
    QPushButton* m_cacheBrowseButton = nullptr;
    QSpinBox* m_cacheDaysSpin = nullptr;
};

} // namespace slideio::viewer::ui
