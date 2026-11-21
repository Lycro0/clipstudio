#include "Updater.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>

#ifndef CLIPSTUDIO_VERSION
#define CLIPSTUDIO_VERSION "0.0.0"
#endif

static const char *kLatestUrl = "https://api.github.com/repos/Lycro0/clipstudio/releases/latest";
static const qint64 kDayMs = 24LL * 60 * 60 * 1000;

static QSettings &Store()
{
	static QSettings s(QSettings::IniFormat, QSettings::UserScope, "ClipStudio", "ClipStudio");
	return s;
}

QString Updater::CurrentVersion()
{
	return QString::fromLatin1(CLIPSTUDIO_VERSION);
}

Updater::Updater(QWidget *w, QObject *parent) : QObject(parent), window(w)
{
	net = new QNetworkAccessManager(this);
	timer = new QTimer(this);
	timer->setInterval(60 * 60 * 1000); // look every hour whether a day has passed
	connect(timer, &QTimer::timeout, this, &Updater::MaybeCheck);
	timer->start();
	QTimer::singleShot(15000, this, &Updater::MaybeCheck); // shortly after startup
}

void Updater::SetEnabled(bool on)
{
	enabled = on;
}

void Updater::MaybeCheck()
{
	if (!enabled)
		return;
	qint64 last = Store().value("updates/lastCheck", 0).toLongLong();
	if (QDateTime::currentMSecsSinceEpoch() - last >= kDayMs)
		Check(false);
}

void Updater::Check(bool manual)
{
	if (busy)
		return;
	// Local/dev builds are not numbered releases: nothing to compare against.
	if (CurrentVersion().startsWith("0.0.0")) {
		if (manual)
			emit Status(tr("This is a development build, so update checks are off."));
		return;
	}
	busy = true;
	QNetworkRequest req{QUrl(QString::fromLatin1(kLatestUrl))};
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setHeader(QNetworkRequest::UserAgentHeader, "ClipStudio-Updater");
	QNetworkReply *reply = net->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
		reply->deleteLater();
		busy = false;
		if (reply->error() != QNetworkReply::NoError) {
			if (manual)
				emit Status(tr("Could not check for updates: %1").arg(reply->errorString()));
			return;
		}
		Store().setValue("updates/lastCheck", QDateTime::currentMSecsSinceEpoch());

		QJsonObject rel = QJsonDocument::fromJson(reply->readAll()).object();
		QString tag = rel.value("tag_name").toString();
		QString latest = tag.startsWith('v') ? tag.mid(1) : tag;
		if (QVersionNumber::fromString(latest) <= QVersionNumber::fromString(CurrentVersion())) {
			if (manual)
				emit Status(tr("ClipStudio is up to date (%1).").arg(CurrentVersion()));
			return;
		}

		QString installer;
		for (const QJsonValue &a : rel.value("assets").toArray()) {
			QString name = a.toObject().value("name").toString();
			if (name.startsWith("ClipStudio-Setup", Qt::CaseInsensitive) && name.endsWith(".exe"))
				installer = a.toObject().value("browser_download_url").toString();
		}
		emit UpdateAvailable(latest);
		if (installer.isEmpty())
			return;

		auto answer = QMessageBox::question(
			window, tr("Update available"),
			tr("ClipStudio %1 is available (you have %2).\n\nDownload and install it now? "
			   "ClipStudio will restart.")
				.arg(latest, CurrentVersion()));
		if (answer == QMessageBox::Yes)
			Download(installer, latest);
	});
}

void Updater::Download(const QString &url, const QString &version)
{
	emit Status(tr("Downloading ClipStudio %1...").arg(version));
	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, "ClipStudio-Updater");
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply *reply = net->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, version]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			emit Status(tr("Update download failed: %1").arg(reply->errorString()));
			return;
		}
		QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
				       .filePath(QString("ClipStudio-Setup-%1.exe").arg(version));
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(reply->readAll()) <= 0) {
			emit Status(tr("Could not save the update to %1").arg(path));
			return;
		}
		f.close();
		// Silent install; the installer closes ClipStudio and starts the new version.
		if (QProcess::startDetached(path, {"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART"})) {
			emit Status(tr("Installing ClipStudio %1...").arg(version));
			QTimer::singleShot(500, qApp, &QApplication::quit);
		} else {
			emit Status(tr("Could not start the installer."));
		}
	});
}
