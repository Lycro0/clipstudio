#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

/* System-wide hotkeys (RegisterHotKey), so they work while a game has focus. */
class Hotkeys : public QObject, public QAbstractNativeEventFilter {
	Q_OBJECT

public:
	enum Id { SaveClip = 1, ToggleRecording = 2, ToggleWindow = 3 };

	explicit Hotkeys(QObject *parent = nullptr);
	~Hotkeys() override;

	/* Returns false if the combination is invalid or taken by another app. */
	bool Set(Id id, const QString &sequence);

	bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
	void Triggered(int id);
};
