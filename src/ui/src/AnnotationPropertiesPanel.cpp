#include "slideio/viewer/ui/AnnotationPropertiesPanel.h"

#include "slideio/viewer/app/AnnotationModel.h"
#include "slideio/viewer/core/Iso8601.h"
#include "slideio/viewer/ui/AnnotationPropertiesEdit.h"
#include "slideio/viewer/ui/AnnotationShapeName.h"

#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

namespace slideio::viewer::ui
{

struct AnnotationPropertiesPanel::Impl
{
    app::AnnotationModel* model = nullptr;

    /// The annotation the fields currently describe. commit() targets this, not
    /// the model's selection: on a selection change the model already names the
    /// new annotation while the fields still hold the old one's values.
    std::string shownId;

    QLineEdit* label = nullptr;
    QComboBox* classification = nullptr;
    QPushButton* colorButton = nullptr;
    core::Color color{};
    QSpinBox* lineWidth = nullptr;
    QSlider* fillOpacity = nullptr;
    QPlainTextEdit* notes = nullptr;

    QLabel* type = nullptr;
    QLabel* author = nullptr;
    QLabel* created = nullptr;
    QLabel* modified = nullptr;

    /// True while repopulate() is writing into the widgets, so the signals that
    /// writing emits are not mistaken for the user editing.
    bool populating = false;
};

AnnotationPropertiesPanel::AnnotationPropertiesPanel(QWidget* parent)
    : QDockWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle(tr("Annotation Properties"));

    auto* contents = new QWidget(this);
    auto* form = new QFormLayout(contents);

    m_impl->label = new QLineEdit(contents);
    form->addRow(tr("Label"), m_impl->label);

    m_impl->classification = new QComboBox(contents);
    m_impl->classification->setEditable(true);
    for (const char* name : kAnnotationClassifications) {
        m_impl->classification->addItem(QString::fromLatin1(name));
    }
    // Cleared after populating: the combo must start empty rather than
    // asserting "Tumor" for an annotation nobody has classified.
    m_impl->classification->setCurrentText(QString());
    form->addRow(tr("Classification"), m_impl->classification);

    m_impl->colorButton = new QPushButton(contents);
    m_impl->colorButton->setFlat(true);
    form->addRow(tr("Colour"), m_impl->colorButton);

    m_impl->lineWidth = new QSpinBox(contents);
    m_impl->lineWidth->setRange(1, 5);
    form->addRow(tr("Line width"), m_impl->lineWidth);

    m_impl->fillOpacity = new QSlider(Qt::Horizontal, contents);
    m_impl->fillOpacity->setRange(0, 100);
    form->addRow(tr("Fill opacity"), m_impl->fillOpacity);

    m_impl->notes = new QPlainTextEdit(contents);
    form->addRow(tr("Notes"), m_impl->notes);

    m_impl->type = new QLabel(contents);
    form->addRow(tr("Type"), m_impl->type);
    m_impl->author = new QLabel(contents);
    form->addRow(tr("Author"), m_impl->author);
    m_impl->created = new QLabel(contents);
    form->addRow(tr("Created"), m_impl->created);
    m_impl->modified = new QLabel(contents);
    form->addRow(tr("Modified"), m_impl->modified);

    setWidget(contents);
    setFieldsEnabled(false);

    connect(m_impl->label, &QLineEdit::editingFinished, this, [this]() { commit(); });

    // activated (a deliberate pick from the list) and editingFinished on the
    // editor (free text, on Enter or focus loss) -- NOT currentTextChanged.
    // This combo is editable, so currentTextChanged fires per keystroke: typing
    // "Tumor" would push five undo steps and restamp modifiedAt five times,
    // filling a 50-step stack from one word.
    connect(m_impl->classification, qOverload<int>(&QComboBox::activated), this, [this](int) { commit(); });
    connect(m_impl->classification->lineEdit(), &QLineEdit::editingFinished, this, [this]() { commit(); });

    connect(m_impl->lineWidth, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        if (!m_impl->populating) {
            commit();
        }
    });

    // sliderReleased covers a drag; valueChanged covers the keyboard and the
    // wheel, which never emit sliderReleased at all. Gating valueChanged on
    // isSliderDown() keeps a drag to one undo step instead of one per pixel.
    connect(m_impl->fillOpacity, &QSlider::sliderReleased, this, [this]() { commit(); });
    connect(m_impl->fillOpacity, &QSlider::valueChanged, this, [this](int) {
        if (!m_impl->populating && !m_impl->fillOpacity->isSliderDown()) {
            commit();
        }
    });

    // Notes deliberately do not connect textChanged, which fires per keystroke.
    // They commit on focus loss, through the event filter below.
    m_impl->notes->installEventFilter(this);

    connect(m_impl->colorButton, &QPushButton::clicked, this, [this]() {
        const QColor current(m_impl->color.r, m_impl->color.g, m_impl->color.b);
        const QColor picked = QColorDialog::getColor(current, this, tr("Annotation Colour"));
        if (!picked.isValid()) {
            return;
        }
        // The alpha channel is carried across rather than taken from the dialog:
        // fillOpacity is the slider's job, and Color::a is the stroke's own
        // alpha, which nothing in the UI edits.
        m_impl->color = core::Color{static_cast<uint8_t>(picked.red()), static_cast<uint8_t>(picked.green()),
                                    static_cast<uint8_t>(picked.blue()), m_impl->color.a};
        updateColorSwatch();
        commit();
    });
}

AnnotationPropertiesPanel::~AnnotationPropertiesPanel() = default;

bool AnnotationPropertiesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_impl->notes && event->type() == QEvent::FocusOut) {
        commit();
    }
    return QDockWidget::eventFilter(watched, event);
}

void AnnotationPropertiesPanel::setAnnotationModel(app::AnnotationModel* model)
{
    if (m_impl->model == model) {
        return;
    }
    if (m_impl->model != nullptr) {
        disconnect(m_impl->model, nullptr, this, nullptr);
    }
    m_impl->model = model;
    m_impl->shownId.clear();

    if (m_impl->model != nullptr) {
        // Undo and redo of a properties edit never emit annotationChanged:
        // AnnotationEditCommand::apply removes and re-inserts the annotation. A
        // panel listening only to annotationChanged would show stale text after
        // an undo, so added and removed repopulate too.
        const auto refresh = [this]() { repopulate(); };
        connect(m_impl->model, &app::AnnotationModel::annotationAdded, this, refresh);
        connect(m_impl->model, &app::AnnotationModel::annotationRemoved, this, refresh);
        connect(m_impl->model, &app::AnnotationModel::annotationChanged, this, refresh);
        connect(m_impl->model, &app::AnnotationModel::cleared, this, refresh);
        connect(m_impl->model, &app::AnnotationModel::modelReset, this, refresh);
        connect(m_impl->model, &app::AnnotationModel::selectionChanged, this, [this](const std::string& id) {
            // Commit first: Qt does not guarantee focus-out reaches the field
            // before the new selection arrives, and a label typed and then
            // abandoned by clicking another row must not be lost. commit()
            // targets shownId, which is still the previous annotation here.
            commit();
            repopulate(id != m_impl->shownId);
        });
    }
    repopulate(true);
}

void AnnotationPropertiesPanel::setFieldsEnabled(bool enabled)
{
    m_impl->label->setEnabled(enabled);
    m_impl->classification->setEnabled(enabled);
    m_impl->colorButton->setEnabled(enabled);
    m_impl->lineWidth->setEnabled(enabled);
    m_impl->fillOpacity->setEnabled(enabled);
    m_impl->notes->setEnabled(enabled);
}

void AnnotationPropertiesPanel::updateColorSwatch()
{
    const QColor shown(m_impl->color.r, m_impl->color.g, m_impl->color.b);
    m_impl->colorButton->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid palette(mid);").arg(shown.name()));
    m_impl->colorButton->setText(shown.name().toUpper());
}

void AnnotationPropertiesPanel::repopulate(bool force)
{
    const core::Annotation* annotation =
        m_impl->model == nullptr ? nullptr : m_impl->model->find(m_impl->model->selectedId());

    // Disabling a focused widget sends it a focus-out, which commits the notes;
    // populating covers that too.
    m_impl->populating = true;

    if (annotation == nullptr) {
        m_impl->shownId.clear();
        m_impl->label->clear();
        m_impl->classification->setCurrentText(QString());
        m_impl->notes->clear();
        m_impl->lineWidth->setValue(m_impl->lineWidth->minimum());
        m_impl->fillOpacity->setValue(m_impl->fillOpacity->minimum());
        m_impl->colorButton->setStyleSheet(QString());
        m_impl->colorButton->setText(QString());
        m_impl->type->clear();
        m_impl->author->clear();
        m_impl->created->clear();
        m_impl->modified->clear();
        setFieldsEnabled(false);
        m_impl->populating = false;
        return;
    }

    setFieldsEnabled(true);
    m_impl->shownId = annotation->id();

    const core::AnnotationProperties& properties = annotation->properties();
    // Never written into while it has focus, unless the annotation itself
    // changed: setPlainText and setText move the caret to the start, which
    // would pull it out from under someone typing. A panel commit writes
    // identical values back anyway, since it compared and skipped no-ops.
    if (force || !m_impl->label->hasFocus()) {
        m_impl->label->setText(QString::fromStdString(properties.label));
    }
    if (force || !m_impl->classification->lineEdit()->hasFocus()) {
        m_impl->classification->setCurrentText(QString::fromStdString(properties.classification));
    }
    if (force || !m_impl->notes->hasFocus()) {
        m_impl->notes->setPlainText(QString::fromStdString(properties.notes));
    }
    if (force || !m_impl->lineWidth->hasFocus()) {
        m_impl->lineWidth->setValue(static_cast<int>(properties.lineWidth));
    }
    if (force || !m_impl->fillOpacity->hasFocus()) {
        m_impl->fillOpacity->setValue(static_cast<int>(properties.fillOpacity * 100.0f + 0.5f));
    }
    m_impl->color = properties.color;
    updateColorSwatch();

    m_impl->type->setText(shapeName(*annotation));
    m_impl->author->setText(QString::fromStdString(annotation->metadata().author));
    m_impl->created->setText(QString::fromStdString(core::formatIso8601Utc(annotation->metadata().createdAt)));
    m_impl->modified->setText(QString::fromStdString(core::formatIso8601Utc(annotation->metadata().modifiedAt)));

    m_impl->populating = false;
}

void AnnotationPropertiesPanel::commit()
{
    if (m_impl->populating || m_impl->model == nullptr || m_impl->shownId.empty()) {
        return;
    }
    const std::string id = m_impl->shownId;
    const core::Annotation* annotation = m_impl->model->find(id);
    if (annotation == nullptr) {
        return;
    }

    AnnotationFieldValues fields;
    fields.label = m_impl->label->text().toStdString();
    fields.classification = m_impl->classification->currentText().toStdString();
    fields.notes = m_impl->notes->toPlainText().toStdString();
    fields.color = m_impl->color;
    fields.lineWidth = static_cast<float>(m_impl->lineWidth->value());
    fields.fillOpacity = static_cast<float>(m_impl->fillOpacity->value()) / 100.0f;

    const auto edited = propertiesEdit(annotation->properties(), fields);
    if (!edited.has_value()) {
        return;
    }

    const std::string description = propertiesEditDescription(annotation->properties(), *edited);

    // Emitted, not applied. MainWindow owns the read-only guard and the undo
    // stack, exactly as it does for the list panel's deleteRequested -- one
    // place in the codebase decides whether the open slide may be edited.
    emit propertiesCommitted(id, *edited, description);
}

} // namespace slideio::viewer::ui
