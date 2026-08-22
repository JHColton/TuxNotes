#pragma once

#include "note.h"

#include <QWidget>

class QAction;
class QLabel;
class QPushButton;
class QTextEdit;

class NoteWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NoteWindow(QWidget *parent = nullptr);

    void loadNote(const Note &note);
    Note toNote() const;

    QString caption() const { return windowTitle(); }
    int colorIndex() const { return m_note.colorIndex; }
    bool isCollapsed() const { return m_collapsed; }
    bool isZoomed() const { return m_zoomed; }
    bool isFloating() const { return m_note.floating; }
    bool isTranslucent() const { return m_note.translucent; }

    void setColor(int index);
    void setCollapsed(bool collapsed);
    void setFloating(bool floating);
    void setTranslucent(bool translucent);
    void setSavedFrame(const QRect &globalFrame);

    // The frame this window wants: stored top-left + current size. Authoritative
    // on Wayland where client-side geometry().topLeft() is meaningless.
    QRect desiredFrame() const { return QRect(m_lastKnownTopLeft, size()); }

    // Zoom toggle: fill the compositor maximize area, then restore.
    void toggleZoom();

    // Called by Application once KWin reports actual placed geometry (Wayland)
    // or immediately after native positioning (X11). Reveals the window.
    void revealAt(const QRect &globalFrame);

signals:
    void changed(NoteWindow *self);
    void deleteRequested(NoteWindow *self);
    void newNoteRequested();
    void frameChangeRequested(NoteWindow *self, const QRect &globalFrame);
    void windowFlagsChanged(NoteWindow *self);
    void revealed(NoteWindow *self);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    void buildUi();
    void buildActions();
    void updateCaption();
    void relayout();

    enum Region
    {
        None,
        StripArea,
        CloseBox,
        RollButton,
        ZoomTriangle,
        EdgeLeft,
        EdgeRight,
        EdgeTop,
        EdgeBottom,
        CornerGrip
    };
    Region hitTest(const QPoint &pos) const;
    Qt::Edges edgesForRegion(Region r) const;

    QRect stripRect() const;
    QRect closeBoxRect() const;
    QRect rollButtonRect() const;
    QRect zoomRect() const;
    QRect contentRect() const;

    Note m_note;
    QTextEdit *m_editor = nullptr;

    bool m_collapsed = false;
    bool m_zoomed = false;
    bool m_hoverClose = false;
    bool m_hoverRoll = false;
    bool m_hoverZoom = false;
    int m_expandedHeight = 300;
    QPoint m_lastKnownTopLeft{200, 200};
    QRect m_savedFrame;
    QString m_currentTip;
};
