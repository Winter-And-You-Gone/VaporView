function Component() {
    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        component.loaded.connect(this, Component.prototype.installerLoaded);
    }
}

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

function vaporViewSetWelcomeTargetDirectory(text) {
    var widget = gui.pageWidgetByObjectName("DynamicWelcomeTargetDirectory");
    if (widget === null) {
        return;
    }

    var target = vaporViewDefaultTargetDirectory(text);
    if (target.length === 0) {
        widget.complete = false;
        return;
    }

    if (target !== text && !vaporViewTargetDirectoryUpdateInProgress) {
        vaporViewTargetDirectoryUpdateInProgress = true;
        widget.targetDirectory.text = target;
        vaporViewTargetDirectoryUpdateInProgress = false;
    }

    installer.setValue("TargetDir", target);
    widget.complete = true;
}

Component.prototype.installerLoaded = function() {
    if (installer.addWizardPage(component, "WelcomeTargetDirectory", QInstaller.TargetDirectory)) {
        var welcomePage = gui.pageWidgetByObjectName("DynamicWelcomeTargetDirectory");
        if (welcomePage !== null) {
            welcomePage.targetChooser.clicked.connect(this, Component.prototype.chooseWelcomeTargetDirectory);
            welcomePage.targetDirectory.textChanged.connect(this, Component.prototype.welcomeTargetDirectoryChanged);
            welcomePage.windowTitle = "欢迎";
            welcomePage.targetDirectory.text = vaporViewDefaultTargetDirectory(installer.value("TargetDir"));
            vaporViewSetWelcomeTargetDirectory(welcomePage.targetDirectory.text);
        }
    }

    installer.addWizardPage(component, "ShortcutSelection", QInstaller.ReadyForInstallation);
};

Component.prototype.chooseWelcomeTargetDirectory = function() {
    var widget = gui.pageWidgetByObjectName("DynamicWelcomeTargetDirectory");
    if (widget === null) {
        return;
    }

    var newTarget = QFileDialog.getExistingDirectory("选择 VaporView 安装目录", widget.targetDirectory.text,
                                                     "VaporViewTargetDirectory");
    if (newTarget !== "") {
        widget.targetDirectory.text = vaporViewDefaultTargetDirectory(newTarget);
    }
};

Component.prototype.welcomeTargetDirectoryChanged = function(text) {
    vaporViewSetWelcomeTargetDirectory(text);
};

Component.prototype.createOperations = function() {
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut",
                              "@TargetDir@/VaporView.exe",
                              "@StartMenuDir@/VaporView.lnk",
                              "workingDirectory=@TargetDir@");
        component.addOperation("CreateShortcut",
                              "@TargetDir@/VaporViewSkyTui.exe",
                              "@StartMenuDir@/VaporView Sky TUI.lnk",
                              "workingDirectory=@TargetDir@");

        var createDesktopShortcut = true;
        if (!installer.isCommandLineInstance()) {
            var shortcutPage = component.userInterface("ShortcutSelection");
            if (shortcutPage) {
                createDesktopShortcut = shortcutPage.desktopShortcutCheckBox.checked;
            }
        }
        if (createDesktopShortcut) {
            component.addOperation("CreateShortcut",
                                  "@TargetDir@/VaporView.exe",
                                  "@DesktopDir@/VaporView.lnk",
                                  "workingDirectory=@TargetDir@");
        }
        component.addOperation("CreateShortcut",
                              "@TargetDir@/VaporViewMaintenanceTool.exe",
                              "@TargetDir@/Uninstall VaporView.lnk",
                              "--start-uninstaller",
                              "workingDirectory=@TargetDir@");
    }
};
