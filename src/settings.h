#ifndef SETTINGS_H
#define SETTINGS_H

#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>
#include <QSettings>
#include <QFile>

#ifndef Q_OS_WIN32
#include <unistd.h> // to use config in user's home folder
#include <sys/types.h>
#include <pwd.h>
#include <stdio.h>
#include <string>
#endif

class Settings{
public:
	Settings();
	~Settings();

	void lock();
	void unlock();

	QString getDiskName();

	bool setDiskName(QString disk_name);

	QString getImageName();

	bool setImageName(QString image_name);

	QString getImageMD5();

	bool setImageMD5(QString md5);

	void readConfigFile();
	void writeConfigFile();
	void setStringIfExists(QString option, QString *value);

private:
	// For internal logic
	QMutex		m_qmutex;
	QFile		m_conf_file;
	QSettings	*mp_qsettings;
	QVariant	m_default_qvariant;

	QString		m_disk_name;
	QString		m_image_name;
	QString		m_image_md5;

	bool			m_db_connected;
	bool			m_remember_password;
	/// SQL variables
	QString			m_db_address;
	QString			m_db_username;
	QString			m_db_password;
	QString			m_db_dbname;
	QString			m_db_tablename;
	//QString		m_db_table_prefix;

	/// SQLite variables
	QString			m_db_filename;
};

#endif // SETTINGS_H
