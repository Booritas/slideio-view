#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QPlainTextEdit;

namespace slideio::viewer::ui
{

/// The application's first preferences dialog: the annotation workspace and the
/// user name. Built to be extended; it holds only what E1 needs.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                      const QStringList& categories, QWidget* parent = nullptr);

    [[nodiscard]] QString workspaceDirectory() const;
    [[nodiscard]] QString userName() const;
    [[nodiscard]] QStringList categories() const;

private:
    void browseForWorkspace();

    QLineEdit* m_workspaceEdit = nullptr;
    QLineEdit* m_userNameEdit = nullptr;
    QPlainTextEdit* m_categoriesEdit = nullptr;
};

} // namespace slideio::viewer::ui
