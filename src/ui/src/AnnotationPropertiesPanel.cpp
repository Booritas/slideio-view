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
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include <map>

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

    QLabel* areaValue = nullptr;
    QLabel* lengthValue = nullptr;
    /// The QFormLayout row index of the Area row, so it can be hidden for an
    /// open trace, which has no area at all.
    int areaRow = -1;
    /// The caption of the length row, retitled between "Perimeter" and "Length"
    /// as the shape changes.
    QLabel* lengthCaption = nullptr;

    /// Physical size of one pixel on the open slide. A non-positive component
    /// means the slide carries no calibration and measurements read in pixels.
    core::PixelSize pixelSize;

    /// Self-intersection by annotation id.
    ///
    /// The panel repopulates on annotationChanged, which ViewportWidget emits on
    /// EVERY mouse-move of a drag. The test is O(n^2) -- about a millisecond at
    /// a thousand vertices, but around 125 ms at five thousand -- so running it
    /// per frame would make dragging a long trace unusable. Caching is safe
    /// because self-intersection, area and perimeter are all
    /// translation-invariant: moving a shape cannot change any of them.
    ///
    /// *** VERTEX EDITING MUST INVALIDATE THIS CACHE WHEN IT LANDS. ***
    /// A stale "not self-intersecting" after a reshape would show an area that
    /// disagrees with the shaded region, which is the single failure this whole
    /// feature is designed to prevent.
    std::map<std::string, bool> selfIntersectionById;

    /// True while repopulate() is writing into the widgets, so the signals that
    /// writing emits are not mistaken for the user editing.
    bool populating = false;

    /// False for a slide whose annotations loaded but must never be written —
    /// an unparseable file, or a slide-id mismatch opened read-only. Starts
    /// true so a panel built before any slide is open behaves normally; the
    /// first annotationsAvailableChanged corrects it.
    bool editingAllowed = true;
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
    // Items arrive through setCategories(). Starts empty rather than asserting
    // a category for an annotation nobody has classified.
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

    m_impl->areaValue = new QLabel(contents);
    form->addRow(tr("Area"), m_impl->areaValue);
    m_impl->areaRow = form->rowCount() - 1;

    // One row, two captions: a closed shape has a perimeter, an open trace has
    // a length. PolygonGeometry already uses one flag to mean two shapes, and
    // this follows it.
    m_impl->lengthCaption = new QLabel(tr("Perimeter"), contents);
    m_impl->lengthValue = new QLabel(contents);
    form->addRow(m_impl->lengthCaption, m_impl->lengthValue);

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
        // Perimeter and area are measured from the geometry, so a move or a
        // resize changes what this panel displays even though the list panel
        // can ignore the same signal.
        connect(m_impl->model, &app::AnnotationModel::annotationGeometryChanged, this, refresh);
        // These two mean the contents are being replaced wholesale, so every
        // cached measurement answers for an annotation that may no longer
        // exist -- and ids are not unique across slides.
        const auto refreshAfterReset = [this]() {
            m_impl->selfIntersectionById.clear();
            repopulate(true);
        };
        connect(m_impl->model, &app::AnnotationModel::cleared, this, refreshAfterReset);
        connect(m_impl->model, &app::AnnotationModel::modelReset, this, refreshAfterReset);
        // A retag changes the classification string of whatever is selected, but every
        // annotation still exists and no geometry moved -- so this is an ordinary refresh,
        // and a field the user has focus in stays theirs.
        connect(m_impl->model, &app::AnnotationModel::annotationsBulkChanged, this, refresh);
        connect(m_impl->model, &app::AnnotationModel::selectionChanged, this, [this](const std::string& id) {
            // Commit first: Qt does not guarantee focus-out reaches the field
            // before the new selection arrives, and a label typed and then
            // abandoned by clicking another row must not be lost. commit()
            // targets shownId, which is still the previous annotation here.
            // Read before commit(): a commit that emits re-enters repopulate()
            // through annotationChanged and moves shownId to the new id.
            const bool changed = (id != m_impl->shownId);
            commit();
            repopulate(changed);
        });
    }
    repopulate(true);
}

void AnnotationPropertiesPanel::setFieldsEnabled(bool enabled)
{
    // Two conditions, one switch: a field is usable only when an annotation is
    // selected AND this slide's annotations may be changed at all.
    const bool usable = enabled && m_impl->editingAllowed;
    m_impl->label->setEnabled(usable);
    m_impl->classification->setEnabled(usable);
    m_impl->colorButton->setEnabled(usable);
    m_impl->lineWidth->setEnabled(usable);
    m_impl->fillOpacity->setEnabled(usable);
    m_impl->notes->setEnabled(usable);
}

void AnnotationPropertiesPanel::setEditingAllowed(bool allowed)
{
    if (m_impl->editingAllowed == allowed) {
        return;
    }
    m_impl->editingAllowed = allowed;

    // Going read-only while a field holds an uncommitted edit: drop it rather
    // than commit it. MainWindow would refuse the commit anyway, and refusing
    // puts a transient message on screen for a change the user may have made
    // several slides ago. repopulate(true) writes the stored values back over
    // the focused field, which is what force exists for.
    repopulate(true);
}

void AnnotationPropertiesPanel::setPixelSize(core::PixelSize pixelSize)
{
    m_impl->pixelSize = pixelSize;
    // A different slide means different calibration and a different set of
    // annotations. Ids are not unique across slides, so a surviving entry could
    // answer for the wrong shape.
    m_impl->selfIntersectionById.clear();
    repopulate(true);
}

void AnnotationPropertiesPanel::updateMeasurements(const core::Annotation& annotation)
{
    const bool calibrated = m_impl->pixelSize.x > 0.0 && m_impl->pixelSize.y > 0.0;
    const core::PixelSize scale = calibrated ? m_impl->pixelSize : core::PixelSize{1.0, 1.0};
    const auto units = calibrated ? core::MeasurementUnits::Physical : core::MeasurementUnits::Pixels;

    // Ask the expensive question only when the answer is not already known.
    // Everything measured here is translation-invariant, so an entry stays
    // right for as long as the outline itself is unchanged.
    const auto cached = m_impl->selfIntersectionById.find(annotation.id());
    const core::ShapeMetrics metrics =
        cached == m_impl->selfIntersectionById.end()
            ? core::measure(annotation.geometry(), scale)
            : core::measureKnown(annotation.geometry(), scale, cached->second);

    if (metrics.closed) {
        m_impl->selfIntersectionById[annotation.id()] = metrics.selfIntersecting;
    }

    // widget() is set in the constructor and never cleared, but a QDockWidget
    // with no widget returns null and this would dereference it. The cast
    // already tolerates a null argument, so the guard costs one ternary.
    auto* form = qobject_cast<QFormLayout*>(widget() != nullptr ? widget()->layout() : nullptr);
    if (metrics.closed) {
        if (form != nullptr && m_impl->areaRow >= 0) {
            form->setRowVisible(m_impl->areaRow, true);
        }
        m_impl->lengthCaption->setText(tr("Perimeter"));
        if (metrics.area.has_value()) {
            m_impl->areaValue->setText(
                QString::fromStdString(core::formatArea(*metrics.area, units)));
        } else if (!metrics.selfIntersecting) {
            // Closed, no area, and NOT because it crosses itself -- a shape with
            // too few points reaches this. ShapeMetrics exists to say why an
            // area is absent, and naming a reason that is not the real one is
            // worse than naming none, so say nothing rather than guess.
            m_impl->areaValue->clear();
        } else {
            // Said in the row itself, not in a tooltip. These numbers may reach
            // a report, so the reason one is missing has to be as visible as the
            // number would have been.
            m_impl->areaValue->setText(tr("unavailable (self-intersecting)"));
        }
    } else {
        if (form != nullptr && m_impl->areaRow >= 0) {
            form->setRowVisible(m_impl->areaRow, false);
        }
        m_impl->lengthCaption->setText(tr("Length"));
    }

    m_impl->lengthValue->setText(
        QString::fromStdString(core::formatLength(metrics.outlineLength, units)));
}

void AnnotationPropertiesPanel::updateColorSwatch()
{
    const QColor shown(m_impl->color.r, m_impl->color.g, m_impl->color.b);
    m_impl->colorButton->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid palette(mid);").arg(shown.name()));
    m_impl->colorButton->setText(shown.name().toUpper());
}

void AnnotationPropertiesPanel::showEvent(QShowEvent* event)
{
    QDockWidget::showEvent(event);
    // repopulate() does nothing while hidden. Forced: nothing here is shown
    // yet, so there is no caret to protect and no earlier annotation's text to
    // keep out of a later commit.
    repopulate(true);
}

void AnnotationPropertiesPanel::setCategories(const std::vector<std::string>& categories)
{
    const QString current = m_impl->classification->currentText();
    m_impl->classification->clear();
    for (const std::string& category : categories) {
        m_impl->classification->addItem(QString::fromStdString(category));
    }
    m_impl->classification->setCurrentText(current);
}

void AnnotationPropertiesPanel::commitPendingEdit()
{
    commit();
}

void AnnotationPropertiesPanel::revertToModel()
{
    repopulate(true);
}

void AnnotationPropertiesPanel::repopulate(bool force)
{
    // Hidden docks skip the work: a move-drag emits annotationGeometryChanged per
    // mouse-move, and this does a style-sheet parse and four setters each time.
    // shownId is still cleared, though. The widgets are frozen while hidden, so
    // a shownId left pointing at the last annotation would let a commit (the
    // selectionChanged handler runs one first) write that annotation's stale
    // fields back over whatever the model holds now. Empty makes commit() a
    // no-op, and showEvent() sets it again from the model.
    if (!isVisible()) {
        m_impl->shownId.clear();
        return;
    }

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
        m_impl->areaValue->clear();
        m_impl->lengthValue->clear();
        // The captions too, not only the values. They are set from the LAST
        // annotation shown, so deselecting a polygon used to leave an empty
        // "Area" row and a caption still reading "Perimeter" -- a measurement
        // frame for a shape that is no longer selected. Reset to the same
        // state a line gives, which is what the panel is built with.
        if (auto* emptyForm = qobject_cast<QFormLayout*>(
                widget() != nullptr ? widget()->layout() : nullptr);
            emptyForm != nullptr && m_impl->areaRow >= 0) {
            emptyForm->setRowVisible(m_impl->areaRow, false);
        }
        m_impl->lengthCaption->setText(tr("Length"));
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
        m_impl->lineWidth->setValue(static_cast<int>(properties.lineWidth + 0.5f));
    }
    if (force || !m_impl->fillOpacity->hasFocus()) {
        // Rounded, not truncated: a stored 0.29f times 100 is 28.999998f, which a cast would show as 28,
        // and the next commit would then write 0.28 as a genuine edit on every selection.
        m_impl->fillOpacity->setValue(static_cast<int>(properties.fillOpacity * 100.0f + 0.5f));
    }
    m_impl->color = properties.color;
    updateColorSwatch();

    m_impl->type->setText(shapeName(*annotation));
    m_impl->author->setText(QString::fromStdString(annotation->metadata().author));
    m_impl->created->setText(QString::fromStdString(core::formatIso8601Utc(annotation->metadata().createdAt)));
    m_impl->modified->setText(QString::fromStdString(core::formatIso8601Utc(annotation->metadata().modifiedAt)));

    updateMeasurements(*annotation);

    m_impl->populating = false;
}

void AnnotationPropertiesPanel::commit()
{
    // editingAllowed is checked here as well as on the widgets: the flag can
    // flip while a field holds focus, and commitPendingEdit() is called from
    // MainWindow's aboutToCloseSlide listener, a path that does not go through
    // a widget signal at all.
    if (m_impl->populating || m_impl->model == nullptr || m_impl->shownId.empty()
        || !m_impl->editingAllowed) {
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
