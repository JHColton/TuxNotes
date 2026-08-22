function report(msg) {
    callDBus("__SERVICE__", "/kwin", "__SERVICE__.KWin", "report", msg);
}
try {
    const APP_ID = "__APP_ID__";
    const GEN = __GEN__;
    const targets = __TARGETS__;
    let wins = [];
    for (const w of workspace.windowList()) {
        if (w.resourceClass === APP_ID)
            wins.push(w);
    }
    function findById(id) {
        for (const w of wins) {
            if (String(w.internalId) === id)
                return w;
        }
        return null;
    }
    const results = [];
    for (let t = 0; t < targets.length; ++t) {
        const w = findById(targets[t].id);
        if (!w) {
            // Keep results index-aligned with targets even when a window has
            // not mapped yet — the app relies on positional alignment.
            results.push({ id: targets[t].id, missing: true });
            continue;
        }
        if (targets[t].maximizeArea) {
            // Prefer the window's own output+desktop — the single-window
            // overload can return another output's area on multi-monitor.
            let area = null;
            try {
                const desk = (w.desktops && w.desktops.length > 0)
                    ? w.desktops[0]
                    : workspace.currentDesktopForScreen(w.output);
                area = workspace.clientArea(KWin.MaximizeArea, w.output, desk);
            } catch (e1) {
                area = null;
            }
            if (!area) {
                try {
                    area = workspace.clientArea(KWin.MaximizeArea, w);
                } catch (e2) {
                    area = null;
                }
            }
            if (!area)
                continue;
            w.frameGeometry = { x: Math.round(area.x), y: Math.round(area.y),
                                width: Math.round(area.width), height: Math.round(area.height) };
        } else if (targets[t].frame) {
            w.frameGeometry = targets[t].frame;
        }
        if (typeof targets[t].keepAbove === "boolean")
            w.keepAbove = targets[t].keepAbove;
        if (typeof targets[t].opacity === "number")
            w.opacity = targets[t].opacity;
        const g = w.frameGeometry;
        results.push({ id: targets[t].id,
                       x: Math.round(g.x), y: Math.round(g.y),
                       width: Math.round(g.width), height: Math.round(g.height) });
    }
    report(JSON.stringify({ type: "applied", gen: GEN, results: results }));
    if (results.length > 0) {
        const timer = new QTimer();
        timer.singleShot = true;
        timer.timeout.connect(function () {
            try {
                const confirmed = [];
                for (const r of results) {
                    if (r.missing) {
                        confirmed.push({ id: r.id, missing: true });
                        continue;
                    }
                    const w = findById(r.id);
                    if (!w) {
                        confirmed.push({ id: r.id, missing: true });
                        continue;
                    }
                    const g = w.frameGeometry;
                    confirmed.push({ id: r.id,
                                     x: Math.round(g.x), y: Math.round(g.y),
                                     width: Math.round(g.width), height: Math.round(g.height) });
                }
                report(JSON.stringify({ type: "confirmed", gen: GEN, results: confirmed }));
            } catch (e2) {
                report(JSON.stringify({ type: "error", error: String(e2) }));
            }
        });
        timer.start(__CONFIRM_MS__);
    }
} catch (e) {
    report(JSON.stringify({ type: "error", error: String(e) }));
}
