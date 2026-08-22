function report(msg) {
    callDBus("__SERVICE__", "/kwin", "__SERVICE__.KWin", "report", msg);
}
try {
    const APP_ID = "__APP_ID__";
    function send(kind, w) {
        const g = w.frameGeometry;
        report(JSON.stringify({
            type: kind,
            gen: __GEN__,
            id: String(w.internalId),
            frame: { x: Math.round(g.x), y: Math.round(g.y),
                     width: Math.round(g.width), height: Math.round(g.height) }
        }));
    }
    function hook(w) {
        if (w.resourceClass !== APP_ID)
            return;
        let timer = null;
        w.frameGeometryChanged.connect(function () {
            if (timer)
                timer.stop();
            timer = new QTimer();
            timer.singleShot = true;
            timer.timeout.connect(function () { send("moved", w); });
            timer.start(300);
        });
        send("appeared", w);
    }
    for (const w of workspace.windowList())
        hook(w);
    workspace.windowAdded.connect(hook);
    report(JSON.stringify({ type: "ready", gen: __GEN__ }));
} catch (e) {
    report(JSON.stringify({ type: "error", error: String(e) }));
}
