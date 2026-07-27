function Component() {
    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        installer.addWizardPage(component, "ShortcutSelection", QInstaller.ReadyForInstallation);
    }
}

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
