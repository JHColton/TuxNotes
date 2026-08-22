#include "notewindow.h"

#include "kwinhelper.h"
#include "theme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolTip>
#include <QWindow>

namespace {
constexpr int kStripHeight = 21;
constexpr int kMargin = 5;
constexpr int kEdgeGrip = 5;
constexpr int kGripSize = 14;
} // namespace

NoteWindow::NoteWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
    buildUi();
    buildActions();

    setMouseTracking(true);
    // Collapsed strips are kStripHeight tall; expanded minimum is enforced
    // dynamically so the rolled-up state isn't blocked.
    setMinimumSize(120, kStripHeight);
}

void NoteWindow::buildUi()
{
    m_editor = new QTextEdit(this);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setAcceptRichText(true);
    m_editor->setStyleSheet(QStringLiteral("background: transparent;"));
    m_editor->document()->setDefaultFont(Theme::noteFont());
    m_editor->viewport()->setMouseTracking(false);

    connect(m_editor->document(), &QTextDocument::contentsChange, this, [this](int, int, int) {
        m_note.modified = QDateTime::currentDateTime();
        updateCaption();
        emit changed(this);
    });
}

void NoteWindow::buildActions()
{
    auto add = [this](const QString &text, QKeySequence::StandardKey key, auto slot) {
        QAction *a = new QAction(text, this);
        a->setShortcuts(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };

    add(tr("New Note"), QKeySequence::New, [this] { emit newNoteRequested(); });
    add(tr("Close"), QKeySequence::Close, [this] { emit deleteRequested(this); });

    QAction *collapse =
        new QAction(m_collapsed ? tr("Expand") : tr("Roll Up"), this);
    collapse->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    collapse->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(collapse, &QAction::triggered, this, [this] { setCollapsed(!m_collapsed); });
    addAction(collapse);

    QAction *zoom = new QAction(tr("Zoom"), this);
    zoom->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up));
    zoom->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(zoom, &QAction::triggered, this, [this] { toggleZoom(); });
    addAction(zoom);

    QAction *floating = new QAction(tr("Float on Top"), this);
    floating->setCheckable(true);
    floating->setChecked(m_note.floating);
    floating->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
    floating->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(floating, &QAction::toggled, this, [this](bool on) { setFloating(on); });
    addAction(floating);

    QAction *translucent = new QAction(tr("Translucent"), this);
    translucent->setCheckable(true);
    translucent->setChecked(m_note.translucent);
    translucent->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T));
    translucent->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(translucent, &QAction::toggled, this, [this](bool on) { setTranslucent(on); });
    addAction(translucent);

    for (int i = 0; i < Theme::themes().size(); ++i) {
        QAction *color = new QAction(Theme::themes().at(i).name, this);
        color->setData(i);
        color->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)));
        color->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(color, &QAction::triggered, this, [this, i] { setColor(i); });
        addAction(color);
    }

    QAction *bold = new QAction(tr("Bold"), this);
    bold->setShortcut(QKeySequence::Bold);
    bold->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(bold, &QAction::triggered, this, [this] {
        QTextCursor c = m_editor->textCursor();
        QTextCharFormat f;
        f.setFontWeight(c.charFormat().fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        m_editor->mergeCurrentCharFormat(f);
    });
    addAction(bold);

    QAction *italic = new QAction(tr("Italic"), this);
    italic->setShortcut(QKeySequence::Italic);
    italic->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(italic, &QAction::triggered, this, [this] {
        QTextCursor c = m_editor->textCursor();
        QTextCharFormat f;
        f.setFontItalic(!c.charFormat().fontItalic());
        m_editor->mergeCurrentCharFormat(f);
    });
    addAction(italic);

    QAction *underline = new QAction(tr("Underline"), this);
    underline->setShortcut(QKeySequence::Underline);
    underline->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(underline, &QAction::triggered, this, [this] {
        QTextCursor c = m_editor->textCursor();
        QTextCharFormat f;
        f.setFontUnderline(!c.charFormat().fontUnderline());
        m_editor->mergeCurrentCharFormat(f);
    });
    addAction(underline);
}

void NoteWindow::loadNote(const Note &note)
{
    m_note = note;
    m_lastKnownTopLeft = note.frame.topLeft();
    m_expandedHeight = qMax(note.frame.height(), kStripHeight + 2 * kMargin + 40);

    setWindowTitle(KWinHelper::captionForText(QTextDocument(note.html).toPlainText()));
    m_editor->setHtml(note.html.isEmpty() ? QStringLiteral("<p></p>") : note.html);
    m_editor->document()->setDefaultFont(Theme::noteFont());

    resize(qMax(note.frame.width(), 120),
           note.collapsed ? kStripHeight
                          : qMax(note.frame.height(), kStripHeight + 2 * kMargin + 40));
    setCollapsed(note.collapsed);
    setFloating(note.floating);
    setTranslucent(note.translucent);

    // Suppress the initial contentsChange triggered by setHtml from marking dirty.
    m_note.modified = note.modified;
}

Note NoteWindow::toNote() const
{
    Note n = m_note;
    n.html = m_editor->toHtml();
    n.frame = QRect(m_lastKnownTopLeft, size());
    n.collapsed = m_collapsed;
    n.translucent = m_note.translucent;
    return n;
}

void NoteWindow::setColor(int index)
{
    m_note.colorIndex = index;
    update();
    emit changed(this);
}

void NoteWindow::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;

    if (collapsed) {
        m_expandedHeight = height();
        m_editor->hide();
        resize(width(), kStripHeight);
    } else {
        m_editor->show();
        resize(width(), qMax(m_expandedHeight, kStripHeight + 2 * kMargin + 40));
    }
    relayout();
    updateCaption();
    update();
    emit changed(this);
}

void NoteWindow::setFloating(bool floating)
{
    m_note.floating = floating;
#ifdef Q_OS_LINUX
    if (windowHandle())
        windowHandle()->setFlag(Qt::WindowStaysOnTopHint, floating);
#endif
    emit windowFlagsChanged(this);
    emit changed(this);
}

void NoteWindow::setTranslucent(bool translucent)
{
    m_note.translucent = translucent;
    setWindowOpacity(translucent ? 0.8 : 1.0); // effective on X11; Wayland goes via KWin
    emit windowFlagsChanged(this);
    emit changed(this);
}

void NoteWindow::setSavedFrame(const QRect &globalFrame)
{
    // While zoomed, confirmed placement frames are the zoomed geometry — they
    // must not overwrite the pre-zoom frame we need for restoring.
    if (!m_zoomed)
        m_savedFrame = globalFrame;
    m_lastKnownTopLeft = globalFrame.topLeft();
}

void NoteWindow::revealAt(const QRect &globalFrame)
{
    m_lastKnownTopLeft = globalFrame.topLeft();
    raise();
    show();
    emit revealed(this);
}

void NoteWindow::toggleZoom()
{
    if (m_collapsed)
        return;
    const QRect workArea = screen() ? screen()->availableGeometry()
                                    : QGuiApplication::primaryScreen()->availableGeometry();
    if (!m_zoomed) {
        m_savedFrame = QRect(m_lastKnownTopLeft, size());
        m_zoomed = true;
        // Null rect = fill the compositor's maximize area (panel-aware on Wayland).
        emit frameChangeRequested(this, QRect());
    } else {
        m_zoomed = false;
        const QRect target = m_savedFrame.isValid()
            ? m_savedFrame
            : QRect(workArea.center(), QSize(200, 300));
        emit frameChangeRequested(this, target);
    }
}

void NoteWindow::relayout()
{
    if (!m_collapsed) {
        m_editor->setGeometry(contentRect());
        m_editor->show();
    }
    update();
}

void NoteWindow::updateCaption()
{
    setWindowTitle(KWinHelper::captionForText(m_editor->toPlainText()));
}

QRect NoteWindow::stripRect() const
{
    return QRect(0, 0, width(), kStripHeight);
}

QRect NoteWindow::closeBoxRect() const
{
    return QRect(6, (kStripHeight - 10) / 2 + 1, 10, 10);
}

QRect NoteWindow::rollButtonRect() const
{
    return QRect(width() - 4 - 14 - 8 - 16, (kStripHeight - 12) / 2 + 1, 14, 12);
}

QRect NoteWindow::zoomRect() const
{
    return QRect(width() - 4 - 14, (kStripHeight - 10) / 2 + 1, 14, 10);
}

QRect NoteWindow::contentRect() const
{
    return QRect(kMargin, kStripHeight + kMargin,
                 width() - 2 * kMargin, height() - kStripHeight - 2 * kMargin);
}

NoteWindow::Region NoteWindow::hitTest(const QPoint &pos) const
{
    const QRect r = rect();
    if (!r.contains(pos))
        return None;

    if (closeBoxRect().contains(pos))
        return CloseBox;
    if (rollButtonRect().contains(pos))
        return RollButton;
    if (zoomRect().contains(pos))
        return ZoomTriangle;

    const bool inCorner = pos.x() > r.right() - kGripSize && pos.y() > r.bottom() - kGripSize;
    if (inCorner && !m_collapsed)
        return CornerGrip;

    const bool nearL = pos.x() < r.left() + kEdgeGrip;
    const bool nearR = pos.x() > r.right() - kEdgeGrip;
    const bool nearT = pos.y() < r.top() + kEdgeGrip;
    const bool nearB = pos.y() > r.bottom() - kEdgeGrip;

    if (nearT)
        return EdgeTop;
    if (nearB)
        return EdgeBottom;
    if (nearL)
        return EdgeLeft;
    if (nearR)
        return EdgeRight;

    if (pos.y() < kStripHeight)
        return StripArea;

    return None;
}

Qt::Edges NoteWindow::edgesForRegion(Region region) const
{
    switch (region) {
    case EdgeTop: return Qt::TopEdge;
    case EdgeBottom: return Qt::BottomEdge;
    case EdgeLeft: return Qt::LeftEdge;
    case EdgeRight: return Qt::RightEdge;
    case CornerGrip: return Qt::RightEdge | Qt::BottomEdge;
    default: return {};
    }
}

void NoteWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const NoteTheme &theme = Theme::theme(m_note.colorIndex);

    p.fillRect(rect(), theme.body);

    // title strip
    p.fillRect(stripRect(), theme.titleBar);
    p.setPen(theme.border());
    p.drawLine(0, kStripHeight - 1, width() - 1, kStripHeight - 1);

    // outer border
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // close box
    const QColor boxFill = m_hoverClose ? QColor(255, 255, 255, 230) : QColor(255, 255, 255, 180);
    p.fillRect(closeBoxRect(), boxFill);
    p.setPen(QColor(70, 70, 70));
    p.drawRect(closeBoxRect().adjusted(0, 0, -1, -1));

    // roll-up icon: page with folded (dog-eared) corner
    const QRect roll = rollButtonRect();
    p.setRenderHint(QPainter::Antialiasing, false);
    QColor iconColor(60, 60, 60, m_hoverRoll ? 220 : 150);
    QPen pen(iconColor, 1);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // Page outline with the top-right corner cut off.
    QPolygon page;
    page << QPoint(roll.left(), roll.top())
         << QPoint(roll.right() - 3, roll.top())
         << QPoint(roll.right(), roll.top() + 3)
         << QPoint(roll.right(), roll.bottom())
         << QPoint(roll.left(), roll.bottom());
    p.drawPolygon(page, Qt::OddEvenFill); // outline only: brush disabled
    // Fold triangle.
    QPolygon fold;
    fold << QPoint(roll.right() - 3, roll.top())
         << QPoint(roll.right() - 3, roll.top() + 3)
         << QPoint(roll.right(), roll.top() + 3);
    p.setBrush(iconColor);
    p.drawPolygon(fold);

    // zoom triangle: apex up = expand, apex down = restore
    const QRect z = zoomRect();
    QPolygon tri;
    if (!m_zoomed) {
        tri << QPoint(z.center().x(), z.top())
            << QPoint(z.left() + 1, z.bottom() - 1)
            << QPoint(z.right() - 1, z.bottom() - 1);
    } else {
        tri << QPoint(z.center().x(), z.bottom() - 1)
            << QPoint(z.left() + 1, z.top())
            << QPoint(z.right() - 1, z.top());
    }
    p.setBrush(QColor(60, 60, 60, m_hoverZoom ? 220 : 160));
    p.setPen(Qt::NoPen);
    p.drawPolygon(tri);

    // collapsed caption
    if (m_collapsed) {
        QFont f = Theme::noteFont();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        p.setFont(f);
        p.setPen(QColor(40, 40, 40));
        const QString capText = caption();
        const QFontMetrics fm(f);
        const int avail = width() - closeBoxRect().right() - rollButtonRect().left() - 24;
        p.drawText(QRect(closeBoxRect().right() + 8, 0, avail, kStripHeight),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(capText, Qt::ElideRight, avail));
    }

    // corner grip
    if (!m_collapsed) {
        const QColor gripColor = theme.grip();
        p.setPen(gripColor);
        const int x1 = width() - 2, y1 = height() - 2;
        for (int i = 1; i <= 3; ++i) {
            p.drawLine(x1 - i * 4, y1, x1, y1 - i * 4);
        }
    }
}

void NoteWindow::resizeEvent(QResizeEvent *)
{
    if (m_collapsed && height() != kStripHeight)
        resize(width(), kStripHeight);
    relayout();
    emit changed(this);
}

void NoteWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const Region r = hitTest(e->position().toPoint());
    switch (r) {
    case CloseBox:
        emit deleteRequested(this);
        break;
    case RollButton:
        setCollapsed(!m_collapsed);
        break;
    case ZoomTriangle:
        toggleZoom();
        break;
    case StripArea:
        if (windowHandle())
            windowHandle()->startSystemMove();
        break;
    case EdgeTop:
    case EdgeBottom:
    case EdgeLeft:
    case EdgeRight:
    case CornerGrip:
        if (windowHandle())
            windowHandle()->startSystemResize(edgesForRegion(r));
        break;
    default:
        QWidget::mousePressEvent(e);
    }
}

void NoteWindow::mouseMoveEvent(QMouseEvent *e)
{
    const Region r = hitTest(e->position().toPoint());

    const bool wasHover = m_hoverClose || m_hoverRoll || m_hoverZoom;
    m_hoverClose = r == CloseBox;
    m_hoverRoll = r == RollButton;
    m_hoverZoom = r == ZoomTriangle;
    if (wasHover || m_hoverClose || m_hoverRoll || m_hoverZoom)
        update(stripRect());

    if (r == StripArea || r == CloseBox || r == RollButton || r == ZoomTriangle) {
        setCursor(Qt::ArrowCursor);
        const QString tip = tr("Created: %1\nLast Modified: %2")
                                .arg(QLocale().toString(m_note.created, QLocale::ShortFormat),
                                     QLocale().toString(m_note.modified, QLocale::ShortFormat));
        if (tip != m_currentTip) {
            m_currentTip = tip;
            QToolTip::showText(e->globalPosition().toPoint(), tip, this, stripRect());
        }
    } else {
        m_currentTip.clear();
        QToolTip::hideText();
        switch (r) {
        case EdgeTop:
        case EdgeBottom: setCursor(Qt::SizeVerCursor); break;
        case EdgeLeft:
        case EdgeRight: setCursor(Qt::SizeHorCursor); break;
        case CornerGrip: setCursor(Qt::SizeFDiagCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
        }
    }
    QWidget::mouseMoveEvent(e);
}

void NoteWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && hitTest(e->position().toPoint()) == StripArea) {
        setCollapsed(!m_collapsed);
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void NoteWindow::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);

    QMenu *colorsMenu = menu.addMenu(tr("Color"));
    for (int i = 0; i < Theme::themes().size(); ++i) {
        QAction *a = colorsMenu->addAction(Theme::themes().at(i).name);
        a->setCheckable(true);
        a->setChecked(i == m_note.colorIndex);
        connect(a, &QAction::triggered, this, [this, i] { setColor(i); });
    }

    menu.addSeparator();
    QAction *floatA = menu.addAction(tr("Float on Top"));
    floatA->setCheckable(true);
    floatA->setChecked(m_note.floating);
    connect(floatA, &QAction::toggled, this, [this](bool on) { setFloating(on); });

    QAction *transA = menu.addAction(tr("Translucent"));
    transA->setCheckable(true);
    transA->setChecked(m_note.translucent);
    connect(transA, &QAction::toggled, this, [this](bool on) { setTranslucent(on); });

    menu.addSeparator();
    menu.addAction(tr("Roll Up"), this, [this] { setCollapsed(!m_collapsed); });
    menu.addAction(tr("Zoom"), this, [this] { toggleZoom(); });
    menu.addSeparator();
    menu.addAction(tr("New Note"), this, [this] { emit newNoteRequested(); });
    menu.addAction(tr("Delete Note"), this, [this] { emit deleteRequested(this); });

    menu.exec(e->globalPos());
}
