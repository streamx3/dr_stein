#include <QMessageBox>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <blkid/blkid.h>
#include <openssl/md5.h>

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow){
	ui->setupUi(this);
	m_block_size = 512;

	m_disk_name_editable = false;
	update_input_status();
	ui->label_image_name->setText(cfg.getImageName());
	ui->lineEdit_disk_name->setText(cfg.getDiskName());
	ui->pushButton_restore->setEnabled(!file_empty(cfg.getImageName().toStdString().c_str()));
}

MainWindow::~MainWindow(){
	cfg.writeConfigFile();
	delete ui;
}

void MainWindow::push2browser(QString msg){
	ui->textBrowser->setText(ui->textBrowser->toPlainText() + msg + "\n");
}

void MainWindow::update_input_status(){
	static const char *unlocked = "🔓";
	static const char *locked = "🔒";
	ui->pushButton_lock->setText(m_disk_name_editable ? unlocked : locked);
	ui->lineEdit_disk_name->setEnabled(m_disk_name_editable);
	ui->pushButton_clone->setEnabled(m_disk_name_editable);
}

void MainWindow::__copy(E_ACT action){
	blkid_probe pr;
	blkid_loff_t blk_dev_size;
	struct stat image_stat;
	qint64 sz_dst, sz_copied;
	qint32 fd_disk, fd_image, fd_src, fd_dst;
	ssize_t io_r, io_w;
	quint8 *buf;

	QString summ_stored, summ_calculated;
	QMessageBox::StandardButton reply;

	sz_copied = 0;

	if(action != E_ACT_RESTORE && action != E_ACT_CLONE){
		return;
	}

	buf = (quint8*)malloc(m_block_size);
	// Yes, I did it on purpose;
	// I need no exceptions here
	if(NULL == buf){
		this->push2browser("Ошибка выделения памяти!\nПопробуйте перезагрузиться и повторить снова.");
		return;
	}

	ui->textBrowser->clear();
	ui->progressBar->setValue(0);

	ui->pushButton_clone->setEnabled(false);
	ui->pushButton_restore->setEnabled(false);
	ui->lineEdit_disk_name->setEnabled(false);

	fd_disk = open(ui->lineEdit_disk_name->text().toStdString().c_str(), O_RDWR);
	if(fd_disk > 1){
		push2browser("Открыт целевой диск...");
	}else{
		push2browser("Не удалось открыть целевой диск!");
		return;
	}
	fd_image = stat( ui->label_image_name->text().toStdString().c_str(), &image_stat);
	fd_image = open(ui->label_image_name->text().toStdString().c_str(), O_CREAT | O_RDWR, 0644);
	if(fd_image > 1){
		push2browser("Открыт файл образа...");
	}else{
		push2browser("Не удалось открыть файл образа!");
		::close(fd_disk);
		return;
	}
	pr = blkid_new_probe_from_filename(ui->lineEdit_disk_name->text().toStdString().c_str());
	blk_dev_size = blkid_probe_get_size(pr);


	if(E_ACT_RESTORE == action){
		summ_stored = cfg.getImageMD5();

		push2browser("Вычисление контрольной суммы...");
		summ_calculated = md5sum(cfg.getImageName().toStdString().c_str());

		ui->textBrowser->clear();
		if(summ_calculated == summ_stored && summ_calculated != ""){
			push2browser("Контрольные суммы совпадают, образ в порядке.");
		}else{
			reply = QMessageBox::warning(this, "Внимание!", "Целостность образа нарушена.\nВё равно восстановить?", QMessageBox::No | QMessageBox::Yes);
			if(QMessageBox::No == reply){
				return;
			}
		}
		// Appears to be wrong, while blk_dev_size is based on blocks, not on partition size
//		if(image_stat.st_size != blk_dev_size){
//			reply = QMessageBox::warning(this, "Внимание!", "Размеры диска и его образа не совпадают.\nВё равно восстановить?", QMessageBox::No | QMessageBox::Yes);
//			if(QMessageBox::No == reply){
//				return;
//			}
//		}
	}

////////////////////////////////////////////////////////////////////////////////
	// TODO: switch file/device size determination!
	if(E_ACT_CLONE == action && false == file_empty(ui->label_image_name->text().toStdString().c_str())){
		reply = QMessageBox::warning(this, "Внимание!", "Файл образа не пустой. Перезаписать?", QMessageBox::No | QMessageBox::Yes);
		if(QMessageBox::No == reply){
			update_input_status();
			ui->pushButton_restore->setEnabled(true);
			return;
		}
	}

	m_start_time.start();
	if(action == E_ACT_CLONE){
		fd_src = fd_disk;
		fd_dst = fd_image;
		push2browser("Клонирование...");
		sz_dst = blk_dev_size;
	}else{
		fd_src = fd_image;
		fd_dst = fd_disk;
		push2browser("Восстановление...");
		sz_dst = image_stat.st_size;
	}
	while(sz_copied < sz_dst){
		io_r = read(fd_src, buf, m_block_size);
		if(io_r == -1){
			push2browser("Ошибка чтения!");
			return;
		}
		io_w = write(fd_dst, buf, m_block_size);
		if(io_w == -1 || io_r != io_w){
			this->push2browser("Ошибка записи!");
			if(E_ACT_CLONE == action){
				push2browser("Вероятно, в виду повреждения диска.\nВосстановление не удалось, но можно перезагрузиться и попробовать ещё раз!");
			}
			return;
		}
		sz_copied += io_w;
		update_pb(sz_copied, sz_dst);
	}
////////////////////////////////////////////////////////////////////////////////
	::close(fd_disk);
	::close(fd_image);
	if(E_ACT_CLONE){
		push2browser("Вычисляется контольная сумма образа...");
		cfg.setImageMD5(md5sum(ui->label_image_name->text().toStdString().c_str()));
	}
	////////////////////////////////////////////////////////////////////////////////
	ui->label_time_remaining->setText("готово");
	update_input_status();
	ui->pushButton_restore->setEnabled(true);
	if(E_ACT_CLONE == action){
		push2browser("Копирование успешно завершено!");
	}else{
		push2browser("Восстановление успешно завершено!");
	}
}

void MainWindow::on_pushButton_restore_clicked(){
	__copy(E_ACT_RESTORE);
}

void MainWindow::on_pushButton_clone_clicked(){
	__copy(E_ACT_CLONE);
}

void MainWindow::on_pushButton_lock_clicked(){
	QMessageBox::StandardButton reply;

	if(false == m_disk_name_editable){
		reply = QMessageBox::warning(this, "Внимание!", "Вы собираетесь изменить настройки восстановления!\nВы можете потерять ваши копии и/или сломать операционную систему.\nВы уверены в своих действиях?", QMessageBox::No | QMessageBox::Yes);
		if(reply == QMessageBox::Yes){
			m_disk_name_editable = true;
		}
	}else{
		m_disk_name_editable = false;
	}

	update_input_status();
}

void MainWindow::on_pushButton_md5_clicked(){
	QString summ_stored, summ_calculated;
	summ_stored = cfg.getImageMD5();

	this->push2browser("Вычисление контрольной суммы...");
	summ_calculated = md5sum(cfg.getImageName().toStdString().c_str());

	ui->textBrowser->clear();
	if(summ_calculated == summ_stored && summ_calculated != ""){
		this->push2browser("Контрольные суммы совпадают, образ в порядке.");
	}else{
		this->push2browser("Контрольные суммы не совпадают, восстановление крайне не желательно.");
	}
}

inline void MainWindow::update_time(quint8 percent){
	QString time_str;
	quint64 remaining, delta;
	quint32 days, hours, minutes, seconds;
	delta = (quint64)m_start_time.elapsed(); // msecs

	time_str = QString("");
	if(0 == percent){
		time_str += "долго ещё..."; // bad way to avoid zero division
		return;
	}
	remaining = ((100L - (quint64)percent) * delta) / (quint64)percent;

	remaining /= 1000; // seconds
	seconds = remaining % 60;
	remaining /= 60; // minutes
	minutes = remaining % 60;
	remaining /= 60; // hours
	hours = remaining % 24;
	remaining /= 24; // days
	days = remaining;

	if(days > 0)
		time_str += QString::number(days) + " дней ";
	if(hours > 0)
		time_str += QString::number(hours) + " часов ";
	if(minutes > 0 && days == 0)
		time_str += QString::number(minutes) + " минут ";
	if(seconds > 0 && hours == 0 && days == 0)
		time_str += QString::number(seconds) + " секунд";

	ui->label_time_remaining->setText(time_str);
}

inline void MainWindow::update_pb(quint64 value, quint64 max){
	static quint16 counter = 0; // assignment happen only on first call
	static quint8 percent_last;
	quint8 percent_new;

	if(counter < 2000){
		++counter;
		return;
	}
	if(0 == max){
		max = 1; // bad way to avoid zero division
	}
	percent_new = (quint8)((100L * value ) / max);
	if(percent_new != percent_last){
		ui->progressBar->setValue(percent_new);
	}
	update_time(percent_new);
}

bool file_empty(const char *filename){
	struct stat stat_data;
	int status;

	status = stat(filename, &stat_data);
	return !(status != -1 && stat_data.st_blocks > 0);
}

QString MainWindow::md5sum(const char *filename){
	struct stat stat_data;
	char buf[512];
	unsigned char out[MD5_DIGEST_LENGTH];
	unsigned char out_str[(MD5_DIGEST_LENGTH * 2) + 1];
	ssize_t bytes;
	int fd, i;
	quint64 sz_file, sz_read;
	MD5_CTX c;


	fd = stat(filename, &stat_data);
	if(-1 == fd){
		return QString("");
	}
	sz_file = stat_data.st_size;
	sz_read = 0L;
	fd = open(filename, O_RDONLY);
	if(-1 == fd){
		return QString("");
	}

	MD5_Init(&c);
	bytes = read(fd, buf, 512);
	sz_read += bytes;
	while(bytes > 0){
		MD5_Update(&c, buf, bytes);
		bytes = read(fd, buf, 512);
		sz_read += bytes;
		update_pb(sz_read, sz_file);
	}
	::close(fd);

	MD5_Final(out, &c);

	for(i = 0; i <= MD5_DIGEST_LENGTH; ++i){
		std::sprintf((char*)&out_str[i*2], "%02x", out[i]);
	}
	out_str[(MD5_DIGEST_LENGTH * 2)] = 0;
	return QString((const char*)out_str);
}
