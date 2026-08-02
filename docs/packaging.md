# VaporView packaging

## Windows IFW package

The Windows package is built as an osgEarth-enabled x64 Release from
`build/Release` and then copied into a clean staging directory. The staging
directory is the only input to Qt Installer Framework; it must not be replaced
with the whole build tree because that tree also contains tests, generated
files, local session data, and optional map datasets.

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts\package-ifw-windows.ps1 `
  -IfwBin F:\Qt\QtIFW\4.8.1\bin `
  -RepositoryUrl https://winter-and-you-gone.github.io/VaporView/ifw/windows/x64/repository/
```

When `-RepositoryUrl` is provided, all components are still embedded for the
initial install and the maintenance tool stores that URL for later online
updates. The installer itself always runs in IFW `--offline-only` mode, so it
cannot replace the requested setup version with a newer package from the
remote repository while the initial installation is in progress. Without
`-RepositoryUrl`, it creates a pure offline installer with no update source.

The installer defaults to `C:\VaporView` but keeps the target-directory page
visible so a field deployment can choose another machine-wide path. VaporView's
daily Windows executables are embedded with explicit `asInvoker` manifests; the
installer and maintenance tool may still request elevation while installing,
repairing, or updating files.

The package contains Qt and the osgEarth/OSG/GDAL/PROJ runtime, but does not
contain `resources/maps`. The signed vendor driver installers, when available,
belong under the package `drivers/` folder and are not executed automatically.
The installer creates an empty `data/` directory in the installation root;
ground and Sky recordings use that directory by default.

The Windows package includes `VaporViewPermissionTool.exe` in the installation
root. IFW runs it after file deployment and maintenance-tool replacement to
clear regular-file ReadOnly attributes, grant the initiating interactive user
recursive inheritable Full Control on `@TargetDir@`, and verify that the ACL is
not broadened to `Everyone`, `Users`, or `Authenticated Users`.

## Program updates

VaporView program updates are still applied by Qt Installer Framework through
`VaporViewMaintenanceTool`. The in-app Help > Check for Updates action first
runs the maintenance tool's `check-updates` command against the configured IFW
repository and shows the result inside VaporView. The maintenance wizard is
opened only when an update is reported and the user chooses Update Now.

Development builds that are run directly from the build directory do not include
`VaporViewMaintenanceTool`; install VaporView with the setup package before
testing the full online-update flow.

After a successful Windows update, `VaporViewUpdateRelauncher.exe` restarts
`VaporView.exe` through the current interactive session shell token. If that
non-elevated token cannot be obtained, IFW shows a manual-start message instead
of falling back to an elevated `CreateProcessW` launch.

## Map resource manifest

`packaging/map-resources/manifest.example.json` documents the manifest shape.
The application accepts an HTTP or HTTPS manifest URL from the Map 3D resource
dialog or from `VAPORVIEW_MAP_MANIFEST_URL`.

Each resource has an id, display name, version, required files, and a list of
files. Every file is downloaded to a temporary `.part` path, checked for size
and SHA-256, and atomically moved into the platform map root only after
validation succeeds. Unsafe absolute paths and `..` traversal are rejected.

Windows stores downloaded resources under the installation root. Linux stores
them under the user's Qt application-data directory, while `MapDataManager`
searches both the installation and user-data roots.

## Linux

The `linux-x64-gcc-release` preset and `scripts/package-ifw-linux.sh` are the
first Linux packaging path. Linux does not run the application as root; the
IFW installer may target `/opt/VaporView` with administrator privileges, while
map downloads use the user-data map root.
