#include "slideio/viewer/ui/CalibrationDialog.h"

#include "slideio/viewer/ui/CalibrationPolicy.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

struct CalibrationDialog::Impl
{
    QDoubleSpinBox* x = nullptr;
    QDoubleSpinBox* y = nullptr;
    QCheckBox* square = nullptr;
    QLabel* error = nullptr;
    Outcome outcome = Outcome::Cancelled;
};

namespace
{

QDoubleSpinBox* micronField(QWidget* parent)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setDecimals(4);
    // Wider than the valid range on purpose: the policy, not the spin box,
    // refuses an out-of-range value, so the user is told why instead of
    // having the number silently clamped.
    field->setRange(0.0, 1000.0);
    field->setSingleStep(0.01);
    // \xC2\xB5 is the micro sign: MSVC compiles without /utf-8.
    field->setSuffix(QString::fromUtf8(" \xC2\xB5m/px"));
    return field;
}

} // namespace

CalibrationDialog::CalibrationDialog(double metadataXMetres, double metadataYMetres,
                                     std::optional<core::PixelSize> currentOverride, QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle(tr("Calibration"));

    const core::Calibration metadata = core::effectiveCalibration(metadataXMetres, metadataYMetres, std::nullopt);
    const QString fromMetadata =
        metadata.source == core::CalibrationSource::None
            ? tr("none")
            : QString::fromUtf8("%1 \xC3\x97 %2 \xC2\xB5m/px")
                  .arg(metadata.micronsPerPixel.x, 0, 'f', 4)
                  .arg(metadata.micronsPerPixel.y, 0, 'f', 4);

    auto* form = new QFormLayout;
    form->addRow(tr("From the slide's metadata:"), new QLabel(fromMetadata, this));
    m_impl->x = micronField(this);
    m_impl->y = micronField(this);
    m_impl->square = new QCheckBox(tr("Square pixels"), this);
    form->addRow(tr("Pixel width:"), m_impl->x);
    form->addRow(tr("Pixel height:"), m_impl->y);
    form->addRow(QString(), m_impl->square);

    const CalibrationPrefill prefill = calibrationPrefill(currentOverride, metadataXMetres, metadataYMetres);
    if (prefill.value.has_value()) {
        m_impl->x->setValue(prefill.value->x);
        m_impl->y->setValue(prefill.value->y);
    }
    m_impl->square->setChecked(prefill.squarePixels);
    m_impl->y->setEnabled(!prefill.squarePixels);

    connect(m_impl->square, &QCheckBox::toggled, this, [this](bool square) {
        m_impl->y->setEnabled(!square);
        if (square) {
            m_impl->y->setValue(m_impl->x->value());
        }
    });
    connect(m_impl->x, &QDoubleSpinBox::valueChanged, this, [this](double x) {
        if (m_impl->square->isChecked()) {
            m_impl->y->setValue(x);
        }
    });

    m_impl->error = new QLabel(this);
    m_impl->error->setStyleSheet(QStringLiteral("color: #C0392B;"));
    m_impl->error->setVisible(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* reset = buttons->addButton(tr("Reset to metadata"), QDialogButtonBox::ResetRole);
    reset->setEnabled(currentOverride.has_value());
    connect(reset, &QPushButton::clicked, this, [this]() {
        m_impl->outcome = Outcome::Reset;
        accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        const std::string problem = calibrationInputError(m_impl->x->value(), m_impl->y->value());
        if (!problem.empty()) {
            m_impl->error->setText(QString::fromStdString(problem));
            m_impl->error->setVisible(true);
            return;
        }
        m_impl->outcome = Outcome::Applied;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_impl->error);
    layout->addWidget(buttons);
}

CalibrationDialog::~CalibrationDialog() = default;

CalibrationDialog::Outcome CalibrationDialog::outcome() const
{
    return m_impl->outcome;
}

core::PixelSize CalibrationDialog::value() const
{
    return core::PixelSize{m_impl->x->value(), m_impl->y->value()};
}

} // namespace slideio::viewer::ui
