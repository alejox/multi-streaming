// Presentational: renders rows it is given and emits what the user asked for. It knows
// neither the engine nor libobs objects.
#pragma once

#include <QString>
#include <QWidget>

#include <map>
#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace multistream {

struct DestinationRowStatus {
	QString state;
	QString detail;
	QString stats;
	bool can_start = false;
	bool can_stop = false;
	bool can_edit = false;
};

struct DestinationRowView {
	QString id;
	QString name;
	QString source;
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

private:
	struct RowWidgets {
		QLabel *state = nullptr;
		QLabel *detail = nullptr;
		QLabel *stats = nullptr;
		QPushButton *start = nullptr;
		QPushButton *stop = nullptr;
		QPushButton *edit = nullptr;
		QPushButton *remove = nullptr;
	};

	QWidget *create_row(const DestinationRowView &view);
	static void apply_status(const RowWidgets &widgets, const DestinationRowStatus &status);

	QLabel *problem_ = nullptr;
	QPushButton *dismiss_problem_ = nullptr;
	QLabel *empty_ = nullptr;
	QVBoxLayout *rows_layout_ = nullptr;
	std::map<QString, RowWidgets> rows_;
};

} // namespace multistream
