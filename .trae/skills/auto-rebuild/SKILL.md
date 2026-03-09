---
name: "auto-rebuild"
description: "Automatically rebuilds the VaproView project after code changes. Invoke when code is modified or when user requests a rebuild."
---

# Auto Rebuild

This skill automatically rebuilds the VaproView project to ensure all changes are compiled.

## When to Invoke

**MUST invoke this skill when:**
- Code files have been modified (C++ source files, headers, CMakeLists.txt)
- User explicitly requests a rebuild
- After fixing compilation errors
- After adding new source files to the build system

## What It Does

1. Navigates to the VaproView build directory
2. Runs `cmake ..` to regenerate build files
3. Runs `make -j$(nproc)` to compile the project
4. Reports build status (success/failure)

## Usage

No manual invocation needed. This skill is triggered automatically when code changes are detected.

## Build Location

- Build directory: `/home/nvidia/NAV/VaproView/build`
- Source directory: `/home/nvidia/NAV/VaproView`
