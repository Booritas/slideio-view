#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

namespace slideio::viewer::ui
{

/// The application's first preferences dialog: the annotation workspace and the
/// user name. Built to be extended; it holds only what E1 needs.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                      QWidget* parent = nullptr);

    [[nodiscard]] QString workspaceDirectory() const;
    [[nodiscard]] QString userName() const;

private:
    void browseForWorkspace();

    QLineEdit* m_workspaceEdit = nullptr;
    QLineEdit* m_userNameEdit = nullptr;
};

} // namespace slideio::viewer::ui
