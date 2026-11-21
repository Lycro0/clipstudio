#include "Theme.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStandardPaths>
#include <QStyleFactory>

/* Qt style sheets can only draw arrows/check marks from image files, so draw tiny PNGs once
 * (PNG support is built into Qt, no image plugins needed). */
enum class Icon { Down, Up, Check };

static QString WriteIcon(const QString &name, Icon icon)
{
	const int S = 4; // draw at 4x so they stay crisp on high-DPI screens
	QSize size = icon == Icon::Check ? QSize(12, 12) : QSize(10, 6);
	QPixmap pm(size * S);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	QPen pen(icon == Icon::Check ? QColor("#000000") : QColor("#d0d0d0"), (icon == Icon::Check ? 2.0 : 1.6) * S);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	QPainterPath path;
	if (icon == Icon::Down) {
		path.moveTo(1 * S, 1 * S);
		path.lineTo(5 * S, 5 * S);
		path.lineTo(9 * S, 1 * S);
	} else if (icon == Icon::Up) {
		path.moveTo(1 * S, 5 * S);
		path.lineTo(5 * S, 1 * S);
		path.lineTo(9 * S, 5 * S);
	} else {
		path.moveTo(2.5 * S, 6.2 * S);
		path.lineTo(4.9 * S, 8.6 * S);
		path.lineTo(9.5 * S, 3.6 * S);
	}
	p.drawPath(path);
	p.end();

	QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
	dir.mkpath("theme");
	QString path2 = dir.filePath("theme/" + name);
	pm.save(path2, "PNG");
	return QDir::fromNativeSeparators(path2);
}

void ApplyDarkTheme(QApplication &app)
{
	app.setStyle(QStyleFactory::create("Fusion"));

	QPalette p;
	const QColor bg("#0a0a0a"), panel("#141414"), field("#1c1c1c"), text("#f2f2f2"), muted("#8a8a8a"),
		accent("#ffffff");
	p.setColor(QPalette::Window, bg);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, field);
	p.setColor(QPalette::AlternateBase, panel);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, panel);
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::ToolTipBase, panel);
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::PlaceholderText, muted);
	p.setColor(QPalette::Highlight, accent);
	p.setColor(QPalette::HighlightedText, QColor("#000000"));
	p.setColor(QPalette::Link, accent);
	p.setColor(QPalette::Disabled, QPalette::Text, QColor("#555555"));
	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#555555"));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#555555"));
	app.setPalette(p);

	const QString down = WriteIcon("down.png", Icon::Down);
	const QString up = WriteIcon("up.png", Icon::Up);
	const QString check = WriteIcon("check.png", Icon::Check);

	QString css = R"(
		QWidget { background: #0a0a0a; color: #f2f2f2; }
		QToolTip { background: #141414; color: #f2f2f2; border: 1px solid #333; padding: 4px; }

		QTabWidget::pane { border: none; border-top: 1px solid #262626; }
		QTabBar::tab { background: transparent; color: #8a8a8a; padding: 10px 22px; border: none;
			       border-bottom: 2px solid transparent; }
		QTabBar::tab:hover { color: #d0d0d0; }
		QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #ffffff; }

		QGroupBox { background: #121212; border: 1px solid #262626; border-radius: 10px;
			    margin-top: 1.4em; padding: 14px 12px 10px 12px; }
		QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #ffffff;
				   font-weight: 600; }
		QGroupBox QWidget { background: transparent; }

		QPushButton { background: #1c1c1c; border: 1px solid #333333; border-radius: 8px;
			      padding: 7px 16px; color: #f2f2f2; }
		QPushButton:hover { background: #262626; border-color: #4a4a4a; }
		QPushButton:pressed { background: #303030; }
		QPushButton:disabled { color: #555555; border-color: #222222; }
		QPushButton#primary { background: #ffffff; color: #000000; border: 1px solid #ffffff; font-weight: 600; }
		QPushButton#primary:hover { background: #e0e0e0; }
		QPushButton#primary:disabled { background: #3a3a3a; color: #777777; border-color: #3a3a3a; }

		QComboBox, QLineEdit, QSpinBox, QKeySequenceEdit, QPlainTextEdit, QListWidget {
			background: #1c1c1c; border: 1px solid #2e2e2e; border-radius: 6px; padding: 5px 8px;
			selection-background-color: #ffffff; selection-color: #000000; }
		QComboBox:hover, QLineEdit:hover, QSpinBox:hover { border-color: #4a4a4a; }
		QComboBox:focus, QLineEdit:focus, QSpinBox:focus { border-color: #ffffff; }
		QComboBox::drop-down { border: none; width: 22px; }
		QComboBox::down-arrow { image: url(@DOWN@); width: 10px; height: 6px; }
		QSpinBox { padding-right: 22px; }
		QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 20px;
			background: #262626; border: none; }
		QSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 6px; }
		QSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 6px; }
		QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #3a3a3a; }
		QSpinBox::up-arrow { image: url(@UP@); width: 8px; height: 5px; }
		QSpinBox::down-arrow { image: url(@DOWN@); width: 8px; height: 5px; }
		QComboBox QAbstractItemView { background: #1c1c1c; border: 1px solid #333; outline: none;
					      selection-background-color: #ffffff; selection-color: #000000; }
		QListWidget::item { padding: 8px; border-radius: 6px; }
		QListWidget::item:selected { background: #ffffff; color: #000000; }
		QListWidget::item:hover:!selected { background: #262626; }

		QRadioButton, QCheckBox { spacing: 8px; background: transparent; }
		QRadioButton::indicator, QCheckBox::indicator { width: 14px; height: 14px; border: 2px solid #6b6b6b;
								background: #0a0a0a; }
		QRadioButton::indicator { border-radius: 9px; }
		QCheckBox::indicator { border-radius: 4px; }
		QRadioButton::indicator:hover, QCheckBox::indicator:hover { border-color: #bdbdbd; }
		QRadioButton::indicator:checked { border-color: #ffffff;
			background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,
				stop:0 #ffffff, stop:0.55 #ffffff, stop:0.65 #0a0a0a, stop:1 #0a0a0a); }
		QCheckBox::indicator:checked { border-color: #ffffff; background: #ffffff; image: url(@CHECK@); }

		QSlider::groove:horizontal { height: 4px; background: #333333; border-radius: 2px; }
		QSlider::sub-page:horizontal { background: #ffffff; border-radius: 2px; }
		QSlider::handle:horizontal { background: #ffffff; width: 14px; height: 14px; margin: -6px 0;
					     border-radius: 7px; }

		QProgressBar { background: #1c1c1c; border: none; border-radius: 4px; }
		QProgressBar::chunk { background: #ffffff; border-radius: 4px; }

		QScrollArea { border: none; }
		QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
		QScrollBar::handle:vertical { background: #333333; border-radius: 4px; min-height: 30px; }
		QScrollBar::handle:vertical:hover { background: #4a4a4a; }
		QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
		QScrollBar::add-page, QScrollBar::sub-page { background: none; }
		QSplitter::handle { background: #1c1c1c; }

		QLabel#muted { color: #8a8a8a; }
		QMenu { background: #141414; border: 1px solid #2e2e2e; padding: 4px; }
		QMenu::item { padding: 6px 18px; border-radius: 4px; }
		QMenu::item:selected { background: #ffffff; color: #000000; }
	)";
	css.replace("@DOWN@", down).replace("@UP@", up).replace("@CHECK@", check);
	app.setStyleSheet(css);
}
