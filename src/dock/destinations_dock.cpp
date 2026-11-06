#include "dock/destinations_dock.hpp"

#include <obs-module.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace multistream {

DestinationsDock::DestinationsDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);

	problem_ = new QLabel(this);
	problem_->setWordWrap(true);
	problem_->setTextFormat(Qt::PlainText);
	problem_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	problem_->hide();
	dismiss_problem_ = new QPushButton(obs_module_text("Dock.Dismiss"), this);
	dismiss_problem_->hide();
	connect(dismiss_problem_, &QPushButton::clicked, this, [this] {
		problem_->hide();
		dismiss_problem_->hide();
	});
	layout->addWidget(problem_);
	layout->addWidget(dismiss_problem_);

	empty_ = new QLabel(obs_module_text("Dock.Empty"), this);
	empty_->setWordWrap(true);
	layout->addWidget(empty_);

	auto *rows_container = new QWidget(this);
	rows_layout_ = new QVBoxLayout(rows_container);
	rows_layout_->setContentsMargins(0, 0, 0, 0);
	rows_layout_->addStretch();

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(rows_container);
	layout->addWidget(scroll, 1);

	auto *add = new QPushButton(obs_module_text("Dock.Add"), this);
	auto *start_all = new QPushButton(obs_module_text("Dock.StartAll"), this);
	auto *stop_all = new QPushButton(obs_module_text("Dock.StopAll"), this);
	connect(add, &QPushButton::clicked, this, &DestinationsDock::add_requested);
	connect(start_all, &QPushButton::clicked, this, &DestinationsDock::start_all_requested);
	connect(stop_all, &QPushButton::clicked, this, &DestinationsDock::stop_all_requested);

	auto *buttons = new QHBoxLayout();
	buttons->addWidget(add);
	buttons->addStretch();
	buttons->addWidget(start_all);
	buttons->addWidget(stop_all);
	layout->addLayout(buttons);
}

void DestinationsDock::set_rows(const std::vector<DestinationRowView> &rows)
{
	rows_.clear();
	while (rows_layout_->count() > 1) {
		// Later, because this can run from a click on a button inside the row.
		QLayoutItem *item = rows_layout_->takeAt(0);
		item->widget()->hide();
		item->widget()->deleteLater();
		delete item;
	}

	int position = 0;
	for (const DestinationRowView &view : rows) {
		rows_layout_->insertWidget(position++, create_row(view));
	}
	empty_->setVisible(rows.empty());
}

void DestinationsDock::update_status(const QString &id, const DestinationRowStatus &status)
{
	const auto found = rows_.find(id);
	if (found != rows_.end()) {
		apply_status(found->second, status);
	}
}

void DestinationsDock::show_problem(const QString &problem)
{
	problem_->setText(problem);
	problem_->show();
	dismiss_problem_->show();
}

QWidget *DestinationsDock::create_row(const DestinationRowView &view)
{
	auto *row = new QFrame();
	row->setFrameShape(QFrame::StyledPanel);

	auto *name = new QLabel(view.name, row);
	name->setTextFormat(Qt::PlainText);
	auto *source = new QLabel(view.source, row);

	RowWidgets widgets;
	widgets.state = new QLabel(row);
	widgets.detail = new QLabel(row);
	widgets.detail->setTextFormat(Qt::PlainText);
	widgets.detail->setWordWrap(true);
	widgets.stats = new QLabel(row);
	widgets.start = new QPushButton(obs_module_text("Dock.Start"), row);
	widgets.stop = new QPushButton(obs_module_text("Dock.Stop"), row);
	widgets.edit = new QPushButton(obs_module_text("Dock.Edit"), row);
	widgets.remove = new QPushButton(obs_module_text("Dock.Delete"), row);

	const QString id = view.id;
	connect(widgets.start, &QPushButton::clicked, this, [this, id] { emit start_requested(id); });
	connect(widgets.stop, &QPushButton::clicked, this, [this, id] { emit stop_requested(id); });
	connect(widgets.edit, &QPushButton::clicked, this, [this, id] { emit edit_requested(id); });
	connect(widgets.remove, &QPushButton::clicked, this, [this, id] { emit delete_requested(id); });

	auto *title_line = new QHBoxLayout();
	title_line->addWidget(name, 1);
	title_line->addWidget(source);

	auto *status_line = new QHBoxLayout();
	status_line->addWidget(widgets.state);
	status_line->addStretch();
	status_line->addWidget(widgets.stats);

	auto *button_line = new QHBoxLayout();
	button_line->addWidget(widgets.start);
	button_line->addWidget(widgets.stop);
	button_line->addStretch();
	button_line->addWidget(widgets.edit);
	button_line->addWidget(widgets.remove);

	auto *layout = new QVBoxLayout(row);
	layout->addLayout(title_line);
	layout->addLayout(status_line);
	layout->addWidget(widgets.detail);
	layout->addLayout(button_line);

	apply_status(widgets, view.status);
	rows_.emplace(view.id, widgets);
	return row;
}

void DestinationsDock::apply_status(const RowWidgets &widgets, const DestinationRowStatus &status)
{
	widgets.state->setText(status.state);
	widgets.detail->setText(status.detail);
	widgets.detail->setVisible(!status.detail.isEmpty());
	widgets.stats->setText(status.stats);
	widgets.start->setEnabled(status.can_start);
	widgets.stop->setEnabled(status.can_stop);
	widgets.edit->setEnabled(status.can_edit);
	widgets.remove->setEnabled(status.can_edit);
}

} // namespace multistream
