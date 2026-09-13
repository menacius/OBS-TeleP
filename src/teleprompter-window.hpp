#pragma once

#include "teleprompter-state.hpp"

#include <QTextDocument>
#include <QWidget>

class TeleprompterWindow : public QWidget {
	Q_OBJECT

public:
	explicit TeleprompterWindow(TeleprompterState *state, QWidget *parent = nullptr);

public slots:
	void showOnScreen(int screenIndex);

protected:
	void hideEvent(QHideEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void rebuildDocument();

private:
	void updateDocumentWidth();
	void updateScrollMetrics();
	void paintTeleprompter(QPainter &painter, const QSizeF &canvasSize);
	void paintOverlay(QPainter &painter, const QSizeF &canvasSize);
	void paintProgressBar(QPainter &painter, const QSizeF &canvasSize);

	TeleprompterState *state_ = nullptr;
	QTextDocument document_;
	int documentWidth_ = 0;
	double firstLineBaseline_ = 0.0;
	QString lastScript_;
};
