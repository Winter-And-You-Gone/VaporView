var vaporViewTargetDirectoryHookInstalled = false;

function vaporViewDefaultTargetDirectory(text) {
    if (systemInfo.productType !== "windows") {
        return text;
    }

    var normalized = String(text).replace(/\//g, "\\");
    if (normalized.length === 2 && normalized.charAt(1) === ":") {
        return normalized + "\\VaporView";
    }
    if (normalized.length === 3 &&
        normalized.charAt(1) === ":" &&
        normalized.charAt(2) === "\\") {
        return normalized.substr(0, 2) + "\\VaporView";
    }
    return text;
}

function Controller() {
    installer.setDefaultPageVisible(QInstaller.Introduction, true);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, true);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, true);
}

Controller.prototype.TargetDirectoryPageCallback = function() {
    if (systemInfo.productType !== "windows") {
        return;
    }

    var page = gui.currentPageWidget();
    if (page === null || page.TargetDirectoryLineEdit === undefined) {
        return;
    }

    var lineEdit = page.TargetDirectoryLineEdit;
    var applyDefaultTarget = function(text) {
        var target = vaporViewDefaultTargetDirectory(text);
        if (target !== text) {
            lineEdit.setText(target);
            installer.setValue("TargetDir", target);
        }
    };

    if (!vaporViewTargetDirectoryHookInstalled) {
        vaporViewTargetDirectoryHookInstalled = true;
        lineEdit.textChanged.connect(applyDefaultTarget);
    }
    applyDefaultTarget(lineEdit.text);
};
