function Component() {
    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        component.loaded.connect(this, Component.prototype.installerLoaded);
    }
}

Component.prototype.installerLoaded = function() {
    if (!installer.addWizardPage(component, "ShortcutSelection", QInstaller.ReadyForInstallation)) {
        console.log("Could not add the VaporView shortcut selection page.");
    }
};

Component.prototype.createOperations = function() {
    component.createOperations();

    if (systemInfo.productType === "windows") {
        if (!installer.isCommandLineInstance()) {
            component.addOperation("CreateShortcut",
                                  "@TargetDir@/VaporView.exe",
                                  "@StartMenuDir@/VaporView.lnk",
                                  "workingDirectory=@TargetDir@");
            component.addOperation("CreateShortcut",
                                  "@TargetDir@/VaporViewSkyTui.exe",
                                  "@StartMenuDir@/VaporView Sky TUI.lnk",
                                  "workingDirectory=@TargetDir@");

            var createDesktopShortcut = true;
            var shortcutPage = component.userInterface("ShortcutSelection");
            if (shortcutPage) {
                createDesktopShortcut = shortcutPage.desktopShortcutCheckBox.checked;
            }
            if (createDesktopShortcut) {
                component.addOperation("CreateShortcut",
                                      "@TargetDir@/VaporView.exe",
                                      "@DesktopDir@/VaporView.lnk",
                                      "workingDirectory=@TargetDir@");
            }
        }
        component.addOperation("CreateShortcut",
                              "@TargetDir@/VaporViewMaintenanceTool.exe",
                              "@TargetDir@/Uninstall VaporView.lnk",
                              "--start-uninstaller",
                              "workingDirectory=@TargetDir@");
    }
};
