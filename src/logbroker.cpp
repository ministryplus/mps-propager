#include "logbroker.h"

LogBroker *LogBroker::instance()
{
    static LogBroker broker;
    return &broker;
}

QString LogBroker::formatLine(QtMsgType type, const QString &message)
{
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }
    return QStringLiteral("[%1] %2").arg(QLatin1String(level), message);
}

void LogBroker::post(const QString &line)
{
    emit appended(line);
}
