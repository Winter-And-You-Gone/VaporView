# Isolated Windows WER LocalDumps validation

**Status:** the three real WER crash tests have not been run. The automated
`wer_crash_helper_safe_test` only tests image naming and refusal guards; it
never crashes a process or changes WER settings. A successful self-test is not
evidence of a dump, exception analysis, or WinDbg stack.

Obtain explicit authorization and use an isolated Windows VM, not a developer
workstation or a machine running production processes. Take a VM snapshot and
ensure no VaporView executables or services run in it. WER's image-name keys
also apply to real applications with those names; the stand-ins below contain
only `tests/wer_crash_helper.cpp` and do not connect to any device.

## Preparation

1. In the isolated VM use a trusted checkout and build target `wer_crash_helper`
   with the normal Release toolchain. The automation accepts only that checkout's
   `build/Release/wer_crash_helper.exe`; it checks its self-identification and
   hashes each renamed copy before executing it. This is a trust boundary, not
   an executable signature: protect the checkout, build tools and build output
   from tampering. In a new,
   otherwise empty private test directory, copy **that helper**, not a product
   executable, to each of the three names `VaporView.exe`,
   `VaporViewSkyCore.exe`, and `VaporViewSkyTui.exe`. Compare each copy's SHA-256
   against the helper and execute each with `--describe` to confirm its name.
2. Reserve space for three full process dumps and create a separate empty dump
   root owned by the test account. Verify the crashing process account has
   write permission to each process subdirectory. Do not use any existing dump
   directory: WER replaces its oldest dump when DumpCount is exceeded.
3. In an elevated terminal, inspect **each** of the three per-image keys under
   `HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`.
   Record whether each key exists, and record every value, type, subkey and its
   ACL. For each existing key, use `reg export "HKLM\...\LocalDumps\<image>"`
   to a separate backup file and verify the export succeeded. Abort if the
   original state cannot be preserved or if another process may alter it.
   Record the global LocalDumps values too; do not alter the global key.
4. Enable the project script with an explicit VM dump root and `-DumpCount 10
   -DumpType 2`; run `-Status` with the same arguments. Ensure all three keys
   and their distinct output directories have the expected values and that the
   crashing account can write there.

## Authorized controlled crash and analysis

In the isolated VM only, set the process environment variable
`VAPORVIEW_WER_TEST_CONFIRM=ISOLATED_VM_ONLY`, and invoke each copied helper
with `--trigger-controlled-crash`, one at a time. The helper intentionally
raises the noncontinuable application exception `0xE0425644`. Wait for WER
to finish; record the executable name, start time, exit status, dump path and
nonzero file size. Verify each dump appears under its own process directory.
If no dump appears, report the result rather than assuming WER succeeded.

Open each dump with WinDbg and matching helper symbols. Run `.symfix`,
`.reload`, `!analyze -v`, `.ecxr`, and `k`. Record the image name, exception
code and exception-thread stack; verify the exception is `0xE0425644` and the
stack identifies the helper. Verify that the dump is a full user dump, not
merely that the configured `DumpType` is 2. Keep the actual dumps out of Git.

`scripts/diagnostics/validate-wer-isolated-vm.ps1` automates the stand-in
copies, registry export, ACL backup, configuration, crash, dump discovery,
optional CDB analysis, and restoration. It refuses to execute without both
`-Execute` and `-ConfirmIsolatedVm`, and requires an elevated session and a
new, nonexistent `-TestRoot`. Set `-Helper` to the unrenamed helper and
optionally `-Debugger` to `cdb.exe`. Those explicit switches are intended
only after authorization and VM isolation; they are not proof of either.
The helper exit status must carry the expected `0xE0425644` bit pattern;
this check alone does not prove the dump contains that exception or stack.
The identity string alone is not authentication; a compromised trusted build
output or a concurrent file replacement is outside the script's guarantees.
Review the retained manifest, registry exports, results and debugger output;
they may contain sensitive system information. ACL snapshots cover owner,
group and discretionary access; audit SACLs are not captured, and the script
does not modify ACLs. Full-dump type, exception
thread, stack and the crashing account's write access still require inspection.
The script reports restoration errors but cannot recover an abruptly powered
off VM; use the backup files and recorded ACLs or the VM snapshot if needed.
The helper and debugger have bounded waits. A dump is inspected only after its
size remains stable for a poll interval and it can be opened exclusively;
this check is not a substitute for inspecting the full dump in WinDbg.

## Restore

Remove the environment variable. The automation restores the three managed
values in originally existing keys without deleting their other values or
subkeys; it removes a newly created key only when it contains no unrelated
data. It compares the resulting exports and ACLs against the recorded state.
If a key or ACL changed unexpectedly, retain it and use the backup for manual
review rather than silently discarding other changes. If any
restore step fails, stop and retain the VM and backups for manual repair.
Never delete original user dump files. Keep test dumps until analyzed and
explicitly approved for removal; a VM snapshot alone is not test evidence.

## Account and platform constraints

The default project script resolves `LocalApplicationData` using the account
that runs the elevated configuration script. Environment variables in an
explicit `-DumpFolder` are also expanded using that account; unresolved
variables are rejected. The script stores the resulting absolute path in
HKLM. A product process running as another user, service identity, or system
account may not be able to write there. Specify `-DumpFolder` explicitly when
appropriate, validate access using the *actual crashing account*, and check
space and ACLs; a successful registry status check does not prove WER can write
a dump. WER may also be unavailable due to Windows policy or incompatible
custom crash handling. See Microsoft documentation on [LocalDumps](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps)
and [WinDbg user-mode dump analysis](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/analyzing-a-user-mode-dump-file).
