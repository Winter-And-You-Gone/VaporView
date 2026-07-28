var vaporViewTargetDirectoryHookInstalled = false;
var vaporViewTargetDirectoryUpdateInProgress = false;

function vaporViewDefaultTargetDirectory(text) {
    if (systemInfo.productType !== "windows") {
        return text;
    }

    var normalized = String(text).replace(/\//g, "\\");
    if (normalized.length === 0) {
        return text;
    }

    var trimmed = normalized;
    while (trimmed.length > 3 && trimmed.charAt(trimmed.length - 1) === "\\") {
        trimmed = trimmed.substr(0, trimmed.length - 1);
    }

    var separator = trimmed.lastIndexOf("\\");
    var leaf = separator >= 0 ? trimmed.substr(separator + 1) : trimmed;
    if (/^(VaporView|VaproView)$/i.test(leaf)) {
        return trimmed;
    }

    if (trimmed.length === 2 && trimmed.charAt(1) === ":") {
        return trimmed + "\\VaporView";
    }
    if (trimmed.length === 3 &&
        trimmed.charAt(1) === ":" &&
        trimmed.charAt(2) === "\\") {
        return trimmed.substr(0, 2) + "\\VaporView";
    }
    return trimmed + "\\VaporView";
}

function vaporViewApplyTargetDirectory(lineEdit, text) {
    if (vaporViewTargetDirectoryUpdateInProgress) {
        return;
    }

    var target = vaporViewDefaultTargetDirectory(text);
    if (target !== text) {
        vaporViewTargetDirectoryUpdateInProgress = true;
        lineEdit.setText(target);
        vaporViewTargetDirectoryUpdateInProgress = false;
    }
    installer.setValue("TargetDir", target);
}

function Controller() {
    // Keep installation focused on choices that matter for VaporView.
    // The built-in target directory page stays visible because it provides
    // IFW's most reliable path editor and browse button.
    installer.setDefaultPageVisible(QInstaller.Introduction, false);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, true);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
    installer.setDefaultPageVisible(QInstaller.StartMenuSelection, false);
    installer.setDefaultPageVisible(QInstaller.ReadyForInstallation, false);
}

Controller.prototype.TargetDirectoryPageCallback = function() {
    if (systemInfo.productType !== "windows") {
        return;
    }

    var page = gui.currentPageWidget();
    if (page === null || page.TargetDirectoryLineEdit === undefined) {
        return;
    }

    page.windowTitle = "欢迎";
    var lineEdit = page.TargetDirectoryLineEdit;
    if (!vaporViewTargetDirectoryHookInstalled) {
        vaporViewTargetDirectoryHookInstalled = true;
        lineEdit.textChanged.connect(function(text) {
            vaporViewApplyTargetDirectory(lineEdit, text);
        });
    }
    vaporViewApplyTargetDirectory(lineEdit, lineEdit.text);
};
