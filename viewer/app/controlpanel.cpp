#include "viewer/app/controlpanel.h"

#include <algorithm>

#include <QFormLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace met::app {

ControlPanel::ControlPanel(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);

    header_ = new QToolButton(this);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setArrowType(Qt::DownArrow);
    header_->setText(title);
    header_->setAutoRaise(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(header_, &QToolButton::clicked, this, &ControlPanel::toggleCollapsed);
    outer->addWidget(header_);

    body_ = new QWidget(this);
    bodyLayout_ = new QVBoxLayout(body_);
    bodyLayout_->setContentsMargins(6, 0, 6, 6);
    form_ = new QFormLayout();
    form_->setContentsMargins(0, 0, 0, 0);
    // Let fields fill (and shrink to) the column so wide combos don't push their
    // dropdown off the right edge of a narrow panel.
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form_->setRowWrapPolicy(QFormLayout::DontWrapRows);
    bodyLayout_->addLayout(form_);
    outer->addWidget(body_);
    outer->addStretch(1);
}

void ControlPanel::addRow(const QString& label, QWidget* w) { form_->addRow(label, w); }
void ControlPanel::addRow(QWidget* label, QWidget* w) { form_->addRow(label, w); }
void ControlPanel::addRow(QWidget* w) { form_->addRow(w); }
void ControlPanel::addBlock(QWidget* w) { bodyLayout_->addWidget(w); }

void ControlPanel::setCollapsed(bool collapsed) {
    header_->setArrowType(collapsed ? Qt::RightArrow : Qt::DownArrow);
    body_->setVisible(!collapsed);
}

bool ControlPanel::isCollapsed() const { return !body_->isVisible(); }

void ControlPanel::toggleCollapsed() { setCollapsed(body_->isVisible()); }

ViewFrame::ViewFrame(QWidget* canvas, ControlPanel* panel, QWidget* parent)
    : QWidget(parent), canvas_(canvas), panel_(panel) {
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    canvas->setParent(splitter);
    splitter->addWidget(canvas);

    auto* scroll = new QScrollArea(splitter);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    // A horizontal scrollbar is the last-resort fallback so controls are never
    // clipped even on a very narrow window; normally the min width below prevents it.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    splitter->addWidget(scroll);

    // Give the control section priority over the canvas for width: reserve the
    // panel's natural (font-scaled) width plus room for a vertical scrollbar, so
    // dropdowns and the colorbar legend get the space they need at any DPI/font.
    const int sbw = scroll->verticalScrollBar()->sizeHint().width();
    const int panelW = panel->sizeHint().width() + sbw + 2;
    scroll->setMinimumWidth(panelW);

    splitter->setStretchFactor(0, 1);  // canvas absorbs extra width on resize
    splitter->setStretchFactor(1, 0);  // panel keeps its reserved width
    splitter->setSizes({std::max(canvas->sizeHint().width(), 640), panelW});

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);
}

}  // namespace met::app
