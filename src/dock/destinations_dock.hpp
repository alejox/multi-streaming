// Presentational: renders rows it is given and emits what the user asked for. It knows
// neither the engine nor libobs objects.
#pragma once

#include <QString>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

class QAction;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace multistream {

enum class RowTone { Idle, Busy, Live, Warning, Failed };

struct DestinationRowStatus {
	QString state;
	RowTone tone = RowTone::Idle;
	// Why it failed. Empty hides it.
	QString detail;
	// A warning that does not stop anything. Empty hides it.
	QString warning;
	// Only while active. Empty hides it.
	QString stats;
	bool can_start = false;
	bool can_stop = false;
	bool can_edit = false;
};

struct DestinationRowView {
	QString id;
	QString name;
	QString source;
	// Relative to the plugin's data directory.
	std::string icon_file;
	DestinationRowStatus status;
};

class DestinationsDock final : public QWidget {
	Q_OBJECT

public:
	explicit DestinationsDock(QWidget *parent = nullptr);

	void set_rows(const std::vector<DestinationRowView> &rows);
	void update_status(const QString &id, const DestinationRowStatus &status);
	void show_problem(const QString &problem);

signals:
	void add_requested();
	void start_all_requested();
	void stop_all_requested();
	void start_requested(const QString &id);
	void stop_requested(const QString &id);
	void edit_requested(const QString &id);
	void delete_requested(const QString &id);

protected:
	void changeEvent(QEvent *event) override;

private:
	struct RowWidgets {
		QLabel *icon = nullptr;
		QLabel *dot = nullptr;
		QLabel *state = nullptr;
		QLabel *detail = nullptr;
		QLabel *warning = nullptr;
		QLabel *stats = nullptr;
		QToolButton *toggle = nullptr;
		QToolButton *more = nullptr;
		QAction *edit = nullptr;
		QAction *remove = nullptr;
		std::string icon_file;
		bool stops = false;
		RowTone tone = RowTone::Idle;
	};

	QWidget *create_row(const DestinationRowView &view);
	void apply_status(RowWidgets &widgets, const DestinationRowStatus &status);
	void apply_row_icons(RowWidgets &widgets);

	QLabel *problem_ = nullptr;
	QPushButton *dismiss_problem_ = nullptr;
	QLabel *empty_ = nullptr;
	QVBoxLayout *rows_layout_ = nullptr;
	std::map<QString, RowWidgets> rows_;
};

} // namespace multistream
