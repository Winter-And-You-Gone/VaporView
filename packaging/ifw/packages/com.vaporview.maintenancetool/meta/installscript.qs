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

    if (systemInfo.productType === "windows") {
        var permissionTool = "@TargetDir@/VaporViewPermissionTool.exe";
        if (!installer.versionMatches(vaporViewMaintenanceToolIfwVersion, "<4.8.0")) {
            permissionTool = "@TargetDir@/tmpMaintenanceToolApp/VaporViewPermissionTool.exe";
        }
        component.addOperation("Execute",
                              permissionTool,
                              "apply",
                              "--target-dir",
                              "@TargetDir@",
                              "errormessage=无法在维护工具更新后配置 VaporView 安装目录权限。请查看安装日志。");
        component.addOperation("Execute",
                              permissionTool,
                              "verify",
                              "--target-dir",
                              "@TargetDir@",
                              "errormessage=维护工具更新后的 VaporView 安装目录权限验证失败。请查看安装日志。");
    }
};
