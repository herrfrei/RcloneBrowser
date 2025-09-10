#include "list_of_job_options.h"
#include <QDataStream>
#include <qdir.h>
#include <qlogging.h>
#include <qstandardpaths.h>
#include <utils.h>

static QDataStream &operator>>(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator<<(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e);

static QJsonObject ToJson(JobOptions& jo);
static JobOptions FromJson(const QJsonObject& data);

ListOfJobOptions *ListOfJobOptions::SavedJobOptions = nullptr;
const QString ListOfJobOptions::persistenceFileName = "tasks.bin";
const QString ListOfJobOptions::persistenceFileNameJSON = "tasks.json";

ListOfJobOptions::ListOfJobOptions() {}

ListOfJobOptions *ListOfJobOptions::getInstance() {
  if (SavedJobOptions == nullptr) {
    SavedJobOptions = new ListOfJobOptions();
    RestoreFromUserData(*SavedJobOptions);
  }
  return SavedJobOptions;
}

bool ListOfJobOptions::Persist(JobOptions *jo) {
  bool isNew = !this->tasks.contains(jo);
  if (isNew)
    this->tasks.append(jo);
  else {
    //    int ix = tasks.indexOf(jo);
    //    JobOptions *old = tasks[ix];
    //    qDebug() << QString("old [%1] New [%2]")
    //                    .arg(old->description)
    //                    .arg(jo->description);
  }
  PersistToUserData();
  return isNew;
}

bool ListOfJobOptions::Forget(JobOptions *jo) {
  bool isKnown = this->tasks.contains(jo);
  if (!isKnown)
    return false;
  int ix = tasks.indexOf(jo);
  tasks.removeAt(ix);
  //  qDebug() << QString("removed [%1]").arg(jo->description);
  PersistToUserData();
  return isKnown;
}

QFile *ListOfJobOptions::GetPersistenceFile(QIODevice::OpenModeFlag mode) {

  QDir outputDir;

  if (IsPortableMode()) {
    // in portable mode tasks' file will be saved in the same folder as
    // excecutable
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    outputDir = QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
    // not macOS
    outputDir = QDir(qApp->applicationDirPath());
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    outputDir = QDir(xdg_config_home + "/rclone-browser");
#endif
#endif

  } else {

    // get data location folder from Qt  - OS dependend
    outputDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
  }

  if (!outputDir.exists()) {
    outputDir.mkpath(".");
  }
  QString filePath = outputDir.absoluteFilePath(persistenceFileName);
  QFile *file = new QFile(filePath);

  if (!file->open(mode)) {
    //    qDebug() << QString("Could not open ") << file->fileName();
    delete file;
    file = nullptr;
  }
  return file;
}

QFile* ListOfJobOptions::GetPersistenceFileJSON(QIODevice::OpenModeFlag mode) {
    QDir outputDir;
    
    if (IsPortableMode()) {
        // in portable mode tasks' file will be saved in the same folder as
        // excecutable
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
        outputDir = QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
    // not macOS
        outputDir = QDir(qApp->applicationDirPath());
#else
        QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
        outputDir = QDir(xdg_config_home + "/rclone-browser");
#endif
#endif

    }
    else {

        // get data location folder from Qt  - OS dependend
        outputDir =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }

    if (!outputDir.exists()) {
        outputDir.mkpath(".");
    }
    QString filePath = outputDir.absoluteFilePath(persistenceFileNameJSON);
    QFile* file = new QFile(filePath);

    if (!file->open(mode)) {
        //    qDebug() << QString("Could not open ") << file->fileName();
        delete file;
        file = nullptr;
    }
    return file;
}

bool ListOfJobOptions::RestoreFromUserData(ListOfJobOptions &dataIn) {
	// check for JSON file first
	QFile* fileJSON = GetPersistenceFileJSON(QIODevice::ReadOnly);
	if (fileJSON != nullptr) {
		QByteArray fileData = fileJSON->readAll();
		fileJSON->close();
		delete fileJSON;
		QJsonDocument jdoc = QJsonDocument::fromJson(fileData);
        if (jdoc.isArray()) {
            QJsonArray jarray = jdoc.array();
            for (const QJsonValue& jvalue : jarray) {
                if (jvalue.isObject()) {
                    try {
                        JobOptions* jo = new JobOptions();
                        *jo = FromJson(jvalue.toObject());
                        dataIn.tasks.append(jo);
                    }
                    catch (SerializationException& ex) {
                        //      qDebug() << QString("failed to restore tasks: ") << ex.Message;
                        return false;
                    }
                }
            }
            return true;
		}
	}
	// Read old style binary file
  QFile *file = GetPersistenceFile(QIODevice::ReadOnly);
  if (file == nullptr)
    return false;
  QDataStream instream(file);
  instream.setVersion(QDataStream::Qt_5_2);

  while (!instream.atEnd()) {
    try {
      JobOptions *jo = new JobOptions();
      instream >> *jo;
      dataIn.tasks.append(jo);
    } catch (SerializationException &ex) {
      //      qDebug() << QString("failed to restore tasks: ") << ex.Message;
      file->close();
      delete file;
      return false;
    }
  }

  file->close();
  delete file;

  return true;
}

bool ListOfJobOptions::PersistToUserData() {
  QFile* file = GetPersistenceFileJSON(QIODevice::WriteOnly);
  if (file == nullptr)
    return false;

  QJsonArray jarray;
  for (JobOptions* it : tasks) {
    jarray.push_back(ToJson(*it));
  }
  QJsonDocument jdoc(jarray);
  file->write(jdoc.toJson());  

  file->flush();
  file->close();

  emit tasksListUpdated();

  delete file;

  return true;
}

QJsonObject ToJson(JobOptions& jo) {
    QJsonObject jobject;
    jobject["Version"] = JobOptions::classVersion;
    jobject["Name"] = jo.myName();
    jobject["Description"] = jo.description;
	jobject["JobType"] = static_cast<int>(jo.jobType);
	jobject["Operation"] = static_cast<int>(jo.operation);
	// jobject["DryRun"] = jo.dryRun;
	jobject["Sync"] = jo.sync;
	jobject["SyncTiming"] = static_cast<int>(jo.syncTiming);
	jobject["SkipNewer"] = jo.skipNewer;
	jobject["SkipExisting"] = jo.skipExisting;
	jobject["Compare"] = jo.compare;
	jobject["CompareOption"] = static_cast<int>(jo.compareOption);
	jobject["Verbose"] = jo.verbose;
	jobject["SameFilesystem"] = jo.sameFilesystem;
	jobject["DontUpdateModified"] = jo.dontUpdateModified;
	jobject["Transfers"] = jo.transfers;
	jobject["Checkers"] = jo.checkers;
	jobject["Bandwidth"] = jo.bandwidth;
	jobject["MinSize"] = jo.minSize;
	jobject["MinAge"] = jo.minAge;        
	jobject["MaxAge"] = jo.maxAge;
	jobject["MaxDepth"] = jo.maxDepth;
	jobject["ConnectTimeout"] = jo.connectTimeout;
	jobject["IdleTimeout"] = jo.idleTimeout;
	jobject["Retries"] = jo.retries;
	jobject["LowLevelRetries"] = jo.lowLevelRetries;
	jobject["DeleteExcluded"] = jo.deleteExcluded;
	jobject["Excluded"] = jo.excluded;
	jobject["Extra"] = jo.extra;
	jobject["DriveSharedWithMe"] = jo.DriveSharedWithMe;
	jobject["Source"] = jo.source;
	jobject["Dest"] = jo.dest;
	jobject["IsFolder"] = jo.isFolder;
	jobject["UniqueId"] = jo.uniqueId.toString();

    return jobject;
}

JobOptions FromJson(const QJsonObject& jobject) {
    JobOptions jo;
    int version = jobject["Version"].toInt();
    if (version > JobOptions::classVersion) {
        throw SerializationException("Stored version is newer");
    }
    jo.description = jobject["Description"].toString();
    jo.jobType = static_cast<JobOptions::JobType>(jobject["JobType"].toInt());
    jo.operation = static_cast<JobOptions::Operation>(jobject["Operation"].toInt());
    // jo.dryRun = jobject["DryRun"].toBool();
    jo.sync = jobject["Sync"].toBool();
    jo.syncTiming = static_cast<JobOptions::SyncTiming>(jobject["SyncTiming"].toInt());
    jo.skipNewer = jobject["SkipNewer"].toBool();
    jo.skipExisting = jobject["SkipExisting"].toBool();
    jo.compare = jobject["Compare"].toBool();
    jo.compareOption = static_cast<JobOptions::CompareOption>(jobject["CompareOption"].toInt());
    jo.verbose = jobject["Verbose"].toBool();
    jo.sameFilesystem = jobject["SameFilesystem"].toBool();
    jo.dontUpdateModified = jobject["DontUpdateModified"].toBool();
    jo.transfers = jobject["Transfers"].toString();
    jo.checkers = jobject["Checkers"].toString();
    jo.bandwidth = jobject["Bandwidth"].toString();
    jo.minSize = jobject["MinSize"].toString();
    jo.minAge = jobject["MinAge"].toString();
    jo.maxAge = jobject["MaxAge"].toString();
    jo.maxDepth = jobject["MaxDepth"].toInt();
    jo.connectTimeout = jobject["ConnectTimeout"].toString();
    jo.idleTimeout = jobject["IdleTimeout"].toString();
    jo.retries = jobject["Retries"].toString();
    jo.lowLevelRetries = jobject["LowLevelRetries"].toString();

    jo.deleteExcluded = jobject["DeleteExcluded"].toBool();
    jo.excluded = jobject["Excluded"].toString();
    jo.extra = jobject["Extra"].toString();
    jo.DriveSharedWithMe = jobject["DriveSharedWithMe"].toBool();
    jo.source = jobject["Source"].toString();
    jo.dest = jobject["Dest"].toString();
    jo.isFolder = jobject["IsFolder"].toBool();
    jo.uniqueId = QUuid(jobject["UniqueId"].toString());
    return jo;
}
    
QDataStream &operator<<(QDataStream &stream, JobOptions &jo) {
  stream << jo.myName() << JobOptions::classVersion << jo.description
         << jo.jobType << jo.operation << /* jo.dryRun <<*/ jo.sync
         << jo.syncTiming << jo.skipNewer << jo.skipExisting << jo.compare
         << jo.compareOption << jo.verbose << jo.sameFilesystem
         << jo.dontUpdateModified << jo.transfers << jo.checkers << jo.bandwidth
         << jo.minSize << jo.minAge << jo.maxAge << jo.maxDepth
         << jo.connectTimeout << jo.idleTimeout << jo.retries
         << jo.lowLevelRetries << jo.deleteExcluded << jo.excluded << jo.extra
         << jo.DriveSharedWithMe << jo.source << jo.dest << jo.isFolder
         << jo.uniqueId;

  return stream;
}

QDataStream &operator>>(QDataStream &stream, JobOptions &jo) {
  QString actualName;
  qint32 actualVersion;

  stream >> actualName;
  if (QString::compare(actualName, jo.myName()) != 0)
    throw SerializationException("incorrect class");

  stream >> actualVersion;
  if (actualVersion > JobOptions::classVersion)
    throw SerializationException("stored version is newer");

  stream >> jo.description >> jo.jobType >> jo.operation >>
      /* jo.dryRun >> */ jo.sync >> jo.syncTiming >> jo.skipNewer >>
      jo.skipExisting >> jo.compare >> jo.compareOption >> jo.verbose >>
      jo.sameFilesystem >> jo.dontUpdateModified >> jo.transfers >>
      jo.checkers >> jo.bandwidth >> jo.minSize >> jo.minAge >> jo.maxAge >>
      jo.maxDepth >> jo.connectTimeout >> jo.idleTimeout >> jo.retries >>
      jo.lowLevelRetries >> jo.deleteExcluded >> jo.excluded >> jo.extra >>
      jo.DriveSharedWithMe >> jo.source >> jo.dest;

  // as fields are added in later revisions, check actualVersion here and
  // conditionally extract any new fields iff they are expected based on the
  // stream value
  if (actualVersion >= 2) {
    stream >> jo.isFolder;
    if (actualVersion >= 3) {
      stream >> jo.uniqueId;
    }
  }

  return stream;
}

QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e) {
  in >> (quint32 &)e;
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e) {
  in >> (quint32 &)e;
  return in;
}
