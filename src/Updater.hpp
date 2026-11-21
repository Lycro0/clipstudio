#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;
class QWidget;

/* Checks GitHub Releases for a newer ClipStudio once a day and offers to install it. */
class Updater : public QObject {
	Q_OBJECT

public:
	explicit Updater(QWidget *window, QObject *parent = nullptr);

	void SetEnabled(bool enabled);
	static QString CurrentVersion();

public slots:
	/* manual = the user clicked "Check now" (always reports the result). */
	void Check(bool manual = false);

signals:
	void Status(const QString &message);
	void UpdateAvailable(const QString &version);

private:
	void MaybeCheck();
	void Download(const QString &url, const QString &version);

	QWidget *window;
	QNetworkAccessManager *net;
	QTimer *timer;
	bool enabled = true;
	bool busy = false;
};
