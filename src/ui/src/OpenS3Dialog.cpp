#include "slideio/viewer/ui/OpenS3Dialog.h"

#include "slideio/viewer/ui/DriverFilters.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct OpenS3Dialog::Impl
{
    QLineEdit* urlEdit = nullptr;
    QComboBox* driverCombo = nullptr;
    QPushButton* openButton = nullptr;
};

OpenS3Dialog::OpenS3Dialog(QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle("Open Slide from S3");
    setModal(true);

    m_impl->urlEdit = new QLineEdit(this);
    m_impl->urlEdit->setPlaceholderText(
        "https://bucket.s3.amazonaws.com/path/slide.svs?X-Amz-Signature=...");

    m_impl->driverCombo = new QComboBox(this);
    // Reuse the named-driver entries from the Open dialog's filter list, but
    // drop the "All Supported"/"All Files" auto entries (empty driverId): a
    // presigned URL has no usable extension, so the driver must be explicit.
    for (const DriverFilter& f : availableDriverFilters()) {
        if (f.driverId.isEmpty()) {
            continue;
        }
        m_impl->driverCombo->addItem(f.displayName, f.driverId);
    }

    auto* form = new QFormLayout;
    form->addRow("Presigned &URL:", m_impl->urlEdit);
    form->addRow("&Driver:", m_impl->driverCombo);

    auto* buttonBox = new QDialogButtonBox(this);
    m_impl->openButton = buttonBox->addButton("Open Slide", QDialogButtonBox::AcceptRole);
    m_impl->openButton->setDefault(true);
    buttonBox->addButton(QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Open Slide stays disabled until the URL field has non-whitespace content,
    // so a load is never started without a URL.
    auto updateOpenEnabled = [this]() {
        m_impl->openButton->setEnabled(!m_impl->urlEdit->text().trimmed().isEmpty());
    };
    connect(m_impl->urlEdit, &QLineEdit::textChanged, this, updateOpenEnabled);
    updateOpenEnabled();
}

OpenS3Dialog::~OpenS3Dialog() = default;

std::string OpenS3Dialog::presignedUrl() const
{
    return m_impl->urlEdit->text().trimmed().toStdString();
}

std::string OpenS3Dialog::driverId() const
{
    return m_impl->driverCombo->currentData().toString().toStdString();
}

} // namespace slideio::viewer::ui
