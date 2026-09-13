#include "teleprompter-window.hpp"

#include "teleprompter-locale.hpp"

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QHideEvent>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QStaticText>

namespace {
void applyOutputTransform(QPainter &painter, const TeleprompterStyle &style, const QSizeF &canvasSize)
{
	if (!style.mirrorHorizontal && !style.mirrorVertical)
		return;

	painter.translate(style.mirrorHorizontal ? canvasSize.width() : 0, style.mirrorVertical ? canvasSize.height() : 0);
	painter.scale(style.mirrorHorizontal ? -1.0 : 1.0, style.mirrorVertical ? -1.0 : 1.0);
}
}

TeleprompterWindow::TeleprompterWindow(TeleprompterState *state, QWidget *parent) : QWidget(parent), state_(state)
{
	setWindowTitle(Tr("Output.Title"));
	setWindowFlag(Qt::Window);
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::BlankCursor);

	connect(state_, &TeleprompterState::scriptChanged, this, &TeleprompterWindow::rebuildDocument);
	connect(state_, &TeleprompterState::styleChanged, this, &TeleprompterWindow::rebuildDocument);
	connect(state_, &TeleprompterState::displayChanged, this, QOverload<>::of(&TeleprompterWindow::update));
	connect(state_, &TeleprompterState::positionChanged, this, QOverload<>::of(&TeleprompterWindow::update));
	rebuildDocument();
}

void TeleprompterWindow::showOnScreen(int screenIndex)
{
	const auto screens = QGuiApplication::screens();
	if (screens.isEmpty())
		return;
	const int clamped = qBound(0, screenIndex, screens.size() - 1);
	const QRect geometry = screens[clamped]->geometry();
	setGeometry(geometry);
	showFullScreen();
	raise();
	activateWindow();
}

void TeleprompterWindow::rebuildDocument()
{
	const TeleprompterStyle style = state_->style();
	const QString script = state_->script();
	const bool preserveProgress = !lastScript_.isEmpty() && lastScript_ == script;
	const double oldProgress = state_->progress();
	QFont font(style.fontFamily, style.fontSize);
	document_.setDefaultFont(font);
	document_.setPlainText(script);
	document_.setDocumentMargin(style.margin);
	updateDocumentWidth();

	QTextCursor cursor(&document_);
	cursor.select(QTextCursor::Document);
	QTextBlockFormat blockFormat;
	blockFormat.setLineHeight(style.lineSpacing * 100.0, QTextBlockFormat::ProportionalHeight);
	blockFormat.setBottomMargin(style.paragraphSpacing);
	blockFormat.setAlignment(style.horizontalAlignment);
	cursor.mergeBlockFormat(blockFormat);

	QTextCharFormat charFormat;
	charFormat.setForeground(style.textColor);
	cursor.mergeCharFormat(charFormat);

	updateScrollMetrics();
	if (preserveProgress)
		state_->setPosition(oldProgress * state_->contentHeight());
	lastScript_ = script;
	update();
}

void TeleprompterWindow::updateDocumentWidth()
{
	const int nextWidth = qMax(100, width());
	if (documentWidth_ == nextWidth)
		return;
	documentWidth_ = nextWidth;
	document_.setTextWidth(documentWidth_);
	updateScrollMetrics();
}

void TeleprompterWindow::updateScrollMetrics()
{
	// QTextLayout line data is populated lazily. Force a full layout before
	// using lineCount(), or a valid script can incorrectly get a zero range.
	document_.documentLayout()->documentSize();

	double firstBaseline = 0.0;
	double lastBaseline = 0.0;
	bool foundText = false;

	for (QTextBlock block = document_.begin(); block.isValid(); block = block.next()) {
		const QTextLayout *layout = block.layout();
		if (block.text().trimmed().isEmpty() || !layout || layout->lineCount() == 0)
			continue;

		const QRectF blockRect = document_.documentLayout()->blockBoundingRect(block);
		const QTextLine firstLine = layout->lineAt(0);
		const QTextLine lastLine = layout->lineAt(layout->lineCount() - 1);
		const double blockFirstBaseline = blockRect.top() + firstLine.y() + firstLine.ascent();
		const double blockLastBaseline = blockRect.top() + lastLine.y() + lastLine.ascent();
		if (!foundText) {
			firstBaseline = blockFirstBaseline;
			foundText = true;
		}
		lastBaseline = blockLastBaseline;
	}

	firstLineBaseline_ = foundText ? firstBaseline : 0.0;
	state_->setContentHeight(foundText ? qMax(0.0, lastBaseline - firstBaseline) : 0.0);
}

void TeleprompterWindow::paintEvent(QPaintEvent *)
{
	const TeleprompterStyle style = state_->style();
	QPainter painter(this);
	painter.fillRect(rect(), style.backgroundColor);

	const double renderScale = state_->outputRenderScale();
	if (renderScale < 0.99) {
		const QSize imageSize(qMax(1, int(width() * renderScale)), qMax(1, int(height() * renderScale)));
		QImage image(imageSize, QImage::Format_RGB32);
		image.fill(style.backgroundColor);
		QPainter imagePainter(&image);
		imagePainter.scale(renderScale, renderScale);
		paintTeleprompter(imagePainter, size());
		imagePainter.end();
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter.drawImage(rect(), image);
		return;
	}

	paintTeleprompter(painter, size());
}

void TeleprompterWindow::paintTeleprompter(QPainter &painter, const QSizeF &canvasSize)
{
	const TeleprompterStyle style = state_->style();
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	painter.setRenderHint(QPainter::Antialiasing, true);

	painter.save();
	applyOutputTransform(painter, style, canvasSize);

	double y = -state_->position();
	if (style.showPositionIndicator) {
		y += canvasSize.height() / 2.0 - firstLineBaseline_;
	} else if (style.verticalAlignment & Qt::AlignVCenter) {
		y += (canvasSize.height() - document_.size().height()) / 2.0;
	} else if (style.verticalAlignment & Qt::AlignBottom) {
		y += canvasSize.height() - document_.size().height();
	}

	const QRectF visibleRect(0, -y, canvasSize.width(), canvasSize.height());
	painter.translate(0, y);
	document_.drawContents(&painter, visibleRect);
	painter.restore();

	if (style.showPositionIndicator) {
		QPen pen(QColor(255, 64, 64, 180), 3);
		painter.setPen(pen);
		const int indicatorY = int(canvasSize.height() / 2.0);
		painter.drawLine(24, indicatorY, int(canvasSize.width()) - 24, indicatorY);
	}

	paintProgressBar(painter, canvasSize);

	painter.save();
	applyOutputTransform(painter, style, canvasSize);
	paintOverlay(painter, canvasSize);
	painter.restore();
}

void TeleprompterWindow::paintProgressBar(QPainter &painter, const QSizeF &canvasSize)
{
	const TeleprompterProgressBarSettings progressBar = state_->progressBarSettings();
	if (!progressBar.enabled)
		return;

	const int thickness = qBound(2, progressBar.thickness, 80);
	const int x = progressBar.position == 0 ? 0 : int(canvasSize.width()) - thickness;
	const QRectF track(x, 0, thickness, canvasSize.height());
	const double filledHeight = canvasSize.height() * state_->progress();
	const QRectF fill(x, canvasSize.height() - filledHeight, thickness, filledHeight);
	QColor trackColor = progressBar.color;
	trackColor.setAlpha(qMin(90, qMax(28, progressBar.color.alpha() / 3)));

	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(Qt::NoPen);
	painter.setBrush(trackColor);
	painter.drawRect(track);
	painter.setBrush(progressBar.color);
	painter.drawRect(fill);
	painter.restore();
}

void TeleprompterWindow::paintOverlay(QPainter &painter, const QSizeF &canvasSize)
{
	const TeleprompterOverlaySettings overlay = state_->overlaySettings();
	if (!overlay.enabled)
		return;

	QStringList parts;
	if (overlay.showTitle)
		parts << state_->title();
	if (overlay.showPlaybackState)
		parts << (state_->isPlaying() ? QStringLiteral("PLAYING") : QStringLiteral("PAUSED"));
	if (overlay.showSpeed)
		parts << QStringLiteral("%1 px/s").arg(state_->speed(), 0, 'f', 0);
	if (overlay.showProgress)
		parts << QStringLiteral("%1%").arg(state_->progress() * 100.0, 0, 'f', 0);
	if (parts.isEmpty())
		return;

	const QString text = parts.join(QStringLiteral("  |  "));
	QFont font(QStringLiteral("Arial"), 18, QFont::DemiBold);
	QFontMetrics metrics(font);
	const int paddingX = 18;
	const int paddingY = 12;
	const int accentWidth = 5;
	const QSize textSize = metrics.size(Qt::TextSingleLine, text);
	const QSize panelSize(textSize.width() + paddingX * 2 + accentWidth, textSize.height() + paddingY * 2);
	const int edge = 28;
	QPoint topLeft(edge, edge);
	const int maxX = int(canvasSize.width()) - panelSize.width() - edge;
	const int maxY = int(canvasSize.height()) - panelSize.height() - edge;

	switch (overlay.position) {
	case 0:
		topLeft = QPoint(edge, edge);
		break;
	case 1:
		topLeft = QPoint(maxX, edge);
		break;
	case 2:
		topLeft = QPoint(edge, maxY);
		break;
	case 3:
		topLeft = QPoint(maxX, maxY);
		break;
	case 4:
		topLeft = QPoint((int(canvasSize.width()) - panelSize.width()) / 2, edge);
		break;
	case 5:
		topLeft = QPoint((int(canvasSize.width()) - panelSize.width()) / 2, maxY);
		break;
	default:
		break;
	}

	QRect panel(topLeft, panelSize);
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(11, 18, 32, 220));
	painter.drawRoundedRect(panel, 7, 7);
	painter.setBrush(QColor(14, 165, 255));
	painter.drawRoundedRect(QRect(panel.left(), panel.top(), accentWidth, panel.height()), 3, 3);
	painter.setFont(font);
	painter.setPen(QColor(229, 231, 235));
	painter.drawText(panel.adjusted(accentWidth + paddingX, paddingY, -paddingX, -paddingY), Qt::AlignVCenter | Qt::AlignLeft, text);
	painter.restore();
}

void TeleprompterWindow::resizeEvent(QResizeEvent *event)
{
	updateDocumentWidth();
	QWidget::resizeEvent(event);
}

void TeleprompterWindow::hideEvent(QHideEvent *event)
{
	if (state_)
		state_->setOutputFullscreenEnabled(false);
	QWidget::hideEvent(event);
}

void TeleprompterWindow::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Space:
		state_->playPause();
		break;
	case Qt::Key_Escape:
		state_->setOutputFullscreenEnabled(false);
		hide();
		break;
	case Qt::Key_Home:
		state_->jumpToTop();
		break;
	case Qt::Key_Up:
		state_->changeSpeed(5);
		break;
	case Qt::Key_Down:
		state_->changeSpeed(-5);
		break;
	default:
		QWidget::keyPressEvent(event);
	}
}
