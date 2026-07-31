var vaporViewMaintenanceToolIfwVersion = "";

function Component() {
    vaporViewMaintenanceToolIfwVersion = installer.value("FrameworkVersion");
    installer.installationStarted.connect(this, Component.prototype.onInstallationStarted);
}

Component.prototype.onInstallationStarted = function() {
    if (!component.updateRequested() && !component.installationRequested()) {
        return;
    }

    var targetDirectory = installer.value("TargetDir");
    var maintenanceToolDirectory = targetDirectory;
    if (!installer.versionMatches(vaporViewMaintenanceToolIfwVersion, "<4.8.0")) {
        maintenanceToolDirectory += "/tmpMaintenanceToolApp";
    }

    if (installer.value("os") === "win") {
        component.installerbaseBinaryPath = maintenanceToolDirectory + "/installerbase.exe";
    } else if (installer.value("os") === "x11") {
        component.installerbaseBinaryPath = maintenanceToolDirectory + "/installerbase";
    }

    if (component.installerbaseBinaryPath) {
        installer.setInstallerBaseBinary(component.installerbaseBinaryPath);
    }

    installer.setValue("DefaultResourceReplacement", maintenanceToolDirectory + "/update.rcc");
};

Component.prototype.createOperationsForArchive = function(archive) {
    if (installer.versionMatches(vaporViewMaintenanceToolIfwVersion, "<4.8.0")) {
        component.createOperationsForArchive(archive);
    } else {
        component.addOperation("Extract", archive, "@TargetDir@/tmpMaintenanceToolApp");
    }
};
