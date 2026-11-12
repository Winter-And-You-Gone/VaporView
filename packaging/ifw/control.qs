var vaporViewTargetDirectoryHookInstalled = false;

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
        return text;
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

function Controller() {
    // Keep the installer flow compact: the target-directory page is also the
    // welcome page, so users can start installation from a single screen.
    installer.setDefaultPageVisible(QInstaller.Introduction, false);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, true);
    // There is only one mandatory product component; skip the empty selector.
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
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
    if (page.title !== undefined) {
        page.title = qsTr("欢迎安装 VaporView");
    }
    if (page.subtitle !== undefined) {
        page.subtitle = qsTr("选择安装文件夹，然后点击下一步开始安装。");
    }
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
