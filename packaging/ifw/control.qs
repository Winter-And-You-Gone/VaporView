function Controller() {
    // Keep installation focused on choices that matter for VaporView.
    // The package has one forced component and a fixed Start Menu group, so
    // those IFW pages only add clicks without changing the installed result.
    installer.setDefaultPageVisible(QInstaller.Introduction, false);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, false);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
    installer.setDefaultPageVisible(QInstaller.StartMenuSelection, false);
    installer.setDefaultPageVisible(QInstaller.ReadyForInstallation, false);
}
