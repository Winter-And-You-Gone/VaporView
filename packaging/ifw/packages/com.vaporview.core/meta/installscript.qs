var vaporViewUpdateCompleted = false;
var vaporViewTargetDirectory = "";
var vaporViewRelaunchScheduled = false;
var vaporViewCoreUpdateRequested = false;

function Component() {
    if (vaporViewTargetDirectory.length === 0) {
        vaporViewTargetDirectory = installer.value("TargetDir");
    }

    if (systemInfo.productType === "windows") {
        component.addStopProcessForUpdateRequest("VaporView.exe");
    }

    if (installer.isUpdater()) {
        installer.installationStarted.connect(this, Component.prototype.rememberCoreUpdateRequest);
        installer.installationFinished.connect(this, Component.prototype.rememberSuccessfulUpdate);
        installer.finishButtonClicked.connect(this, Component.prototype.launchVaporViewAfterUpdate);
        var finishedPage = gui.pageById(QInstaller.InstallationFinished);
        if (finishedPage && finishedPage.left) {
            finishedPage.left.connect(this, Component.prototype.restartVaporViewAfterUpdate);
        }
    }

    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        installer.addWizardPage(component, "ShortcutSelection", QInstaller.ReadyForInstallation);
    }
}

Component.prototype.rememberCoreUpdateRequest = function() {
    vaporViewCoreUpdateRequested = component.updateRequested();
};

Component.prototype.rememberSuccessfulUpdate = function() {
    vaporViewUpdateCompleted = vaporViewCoreUpdateRequested &&
                               installer.status == QInstaller.Success;
    var targetDirectory = installer.value("TargetDir");
    if (targetDirectory.length > 0) {
        vaporViewTargetDirectory = targetDirectory;
    }
};

Component.prototype.launchVaporViewAfterUpdate = function() {
    if (vaporViewRelaunchScheduled) {
        return true;
    }
    if (!vaporViewUpdateCompleted) {
        return false;
    }

    var targetDirectory = vaporViewTargetDirectory;
    if (targetDirectory.length === 0) {
        targetDirectory = installer.value("TargetDir");
    }
    var maintenanceToolPath = targetDirectory + "/VaporViewMaintenanceTool.exe";
    var applicationPath = targetDirectory + "/VaporView.exe";
    var relauncherPath = targetDirectory + "/VaporViewUpdateRelauncher.exe";
    if (installer.fileExists(relauncherPath) && installer.fileExists(applicationPath)) {
        vaporViewRelaunchScheduled = installer.executeDetached(
            relauncherPath,
            [maintenanceToolPath, applicationPath],
            targetDirectory);
        return vaporViewRelaunchScheduled;
    }
    if (installer.fileExists(applicationPath)) {
        vaporViewRelaunchScheduled = installer.executeDetached(applicationPath, [], targetDirectory);
        return vaporViewRelaunchScheduled;
    }
    return false;
};

Component.prototype.restartVaporViewAfterUpdate = function() {
    if (!vaporViewUpdateCompleted) {
        return;
    }
    Component.prototype.launchVaporViewAfterUpdate();
};

Component.prototype.createOperations = function() {
    component.createOperations();

    component.addOperation("Mkdir", "@TargetDir@/data");

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
