#include "settings.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>


Settings::Settings(){
	int fd;
	m_disk_name		= "/tmp/sdc1";
	m_image_md5		= "0";
	m_image_name	= "/opt/dr_stein/backup.img";
	mp_qsettings	= NULL;

#ifndef Q_OS_WIN32
	struct passwd *pw = getpwuid(getuid());

	const char *homedir = pw->pw_dir;
	char filename[512] = { 0 };
	strcpy(filename, homedir);
	strcat(filename, "/.dr_stein/config.ini");
    fd = open(filename, O_RDWR | O_CREAT, 0644);
	if(fd > 0){
		fchmod(fd, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP);
		close(fd);
	}
	if( 0 != access( filename, 0 ) ){
		strcpy(filename, "config.ini");
	}
	m_conf_file.setFileName(filename);
 #else
	m_conf_file.setFileName("config.ini");
#endif
	mp_qsettings = new QSettings( m_conf_file.fileName(), QSettings::IniFormat );
	this->readConfigFile();
}

Settings::~Settings(){
	if( mp_qsettings != 0 )
		delete mp_qsettings;
}

void Settings::lock(){
	return m_qmutex.lock();
}

void Settings::unlock(){
	return m_qmutex.unlock();
}

QString Settings::getDiskName(){
	QMutexLocker locker( &m_qmutex );
	return m_disk_name;
}

bool Settings::setDiskName(QString disk_name){
	QMutexLocker locker( &m_qmutex );
	return m_disk_name = disk_name, 1;
}

QString Settings::getImageName(){
	QMutexLocker locker( &m_qmutex );
	return m_image_name;
}

bool Settings::setImageName(QString image_name){
	QMutexLocker locker( &m_qmutex );
	return m_image_name = image_name, 1;
}

QString Settings::getImageMD5(){
	QMutexLocker locker( &m_qmutex );
	return m_image_md5;
}

bool Settings::setImageMD5(QString md5){
	QMutexLocker locker( &m_qmutex );
	return m_image_md5 = md5, 1;
}

void Settings::setStringIfExists(QString option, QString *value){
	QString tmp;
	QVariant variant;
	if(NULL == value){
		return;
	}

	variant = mp_qsettings->value(option);
	if(variant != m_default_qvariant){
		tmp = variant.toString();
		if(tmp != QString("")){
			*value = tmp;
		}
	}
}

void  Settings::readConfigFile(){
	QMutexLocker locker( &m_qmutex );
	if( m_conf_file.exists() ){
		setStringIfExists("disk_name", &m_disk_name);
		setStringIfExists("image_name", &m_image_name);
		setStringIfExists("image_md5", &m_image_md5);
	}
}

void Settings::writeConfigFile(){
	QMutexLocker locker( &m_qmutex );
	mp_qsettings->setValue( "disk_name", m_disk_name );
	mp_qsettings->setValue( "image_name", m_image_name );
	mp_qsettings->setValue( "image_md5", m_image_md5 );
	mp_qsettings->sync();
}
