#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTime>

#include "settings.h"

enum E_ACT{
	E_ACT_UNKNOWN = 0,
	E_ACT_RESTORE,
	E_ACT_CLONE
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = 0);
	~MainWindow();

	void push2browser(QString msg);

	void update_input_status();

	inline void update_pb(quint64 value, quint64 max);

	QString md5sum(const char *filename);

private slots:
	void on_pushButton_restore_clicked();

	void on_pushButton_clone_clicked();

	void on_pushButton_lock_clicked();

	void on_pushButton_md5_clicked();

private:
	QTime m_start_time;
	Ui::MainWindow *ui;
	Settings cfg;
	bool m_disk_name_editable;
	int m_block_size;


	void __copy(E_ACT action);
	inline void update_time(quint8 percent);
};

bool file_empty(const char *filename);

#endif // MAINWINDOW_H
