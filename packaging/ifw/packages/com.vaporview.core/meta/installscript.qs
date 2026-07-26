function Component() {
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
    }
};
