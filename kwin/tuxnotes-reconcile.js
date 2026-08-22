function report(msg) {
    callDBus("__SERVICE__", "/kwin", "__SERVICE__.KWin", "report", msg);
}
try {
    const APP_ID = "__APP_ID__";
    for (const w of workspace.windowList()) {
        if (w.resourceClass !== APP_ID)
            continue;
        const g = w.frameGeometry;
        report(JSON.stringify({
            type: "appeared",
            gen: __GEN__,
            id: String(w.internalId),
            frame: { x: Math.round(g.x), y: Math.round(g.y),
                     width: Math.round(g.width), height: Math.round(g.height) }
        }));
    }
} catch (e) {
    report(JSON.stringify({ type: "error", error: String(e) }));
}
