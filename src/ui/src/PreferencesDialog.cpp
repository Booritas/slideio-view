#include "slideio/viewer/ui/PreferencesDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

PreferencesDialog::PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    m_workspaceEdit = new QLineEdit(workspaceDirectory, this);
    m_userNameEdit = new QLineEdit(userName, this);
    m_userNameEdit->setPlaceholderText(tr("Required before creating annotations"));

    auto* browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &PreferencesDialog::browseForWorkspace);

    auto* workspaceRow = new QHBoxLayout;
    workspaceRow->addWidget(m_workspaceEdit, 1);
    workspaceRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow(tr("Annotation workspace:"), workspaceRow);
    form->addRow(tr("Your name:"), m_userNameEdit);

    // Said plainly, because the alternative -- moving a user's files as a side
    // effect of changing a setting -- is worse than leaving them where they are.
    auto* note = new QLabel(
        tr("Annotations already saved stay where they are. Changing this folder "
           "affects only annotations saved from now on."),
        this);
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

QString PreferencesDialog::workspaceDirectory() const
{
    return m_workspaceEdit->text().trimmed();
}

QString PreferencesDialog::userName() const
{
    return m_userNameEdit->text().trimmed();
}

void PreferencesDialog::browseForWorkspace()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Choose the annotation workspace folder"), m_workspaceEdit->text());
    if (!chosen.isEmpty()) {
        m_workspaceEdit->setText(chosen);
    }
}

} // namespace slideio::viewer::ui
