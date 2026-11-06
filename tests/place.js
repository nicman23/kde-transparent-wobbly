// Puts the test windows where the drivers expect them.
for (const w of workspace.windowList()) {
    if (w.caption === "Back") {
        w.frameGeometry = {x: 0, y: 0, width: 1280, height: 800};
    }
    if (w.caption === "Front") {
        w.frameGeometry = {x: 300, y: 200, width: 500, height: 380};
        workspace.activeWindow = w;
    }
    if (w.resourceClass.includes("konsole") || w.resourceClass.includes("dolphin")) {
        w.frameGeometry = {x: 300, y: 200, width: 600, height: 400};
        workspace.activeWindow = w;
    }
}
for (const w of workspace.windowList()) {
    console.info("placed", w.caption, w.resourceClass, JSON.stringify(w.frameGeometry));
}
