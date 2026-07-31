function Component() {
    if (installer.isUpdater()) {
        installer.finishButtonClicked.connect(this, Component.prototype.launchVaporViewAfterUpdate);
    }

    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        installer.addWizardPage(component, "ShortcutSelection", QInstaller.ReadyForInstallation);
    }
}

Component.prototype.launchVaporViewAfterUpdate = function() {
    if (installer.status !== QInstaller.Success) {
        return;
    }

    var targetDirectory = installer.value("TargetDir");
    var applicationPath = targetDirectory + "/VaporView.exe";
    if (installer.fileExists(applicationPath)) {
        installer.executeDetached(applicationPath, [], targetDirectory);
    }
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
