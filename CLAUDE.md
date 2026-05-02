# VaporView-QML Workflow Rules

## Build → Commit → Push

每次在此项目中更改代码后，必须依次执行以下步骤：

1. **本地构建** — 在 `build/Release` 目录中执行 `cmake --build . --config Release`，确认编译通过
2. **提交到 Git** — 在仓库根目录执行 `git add` + `git commit`
3. **推送到 GitHub** — `git push origin QML`

注意：构建需要在 Visual Studio 开发者命令提示符环境下进行（先执行 `vcvarsall.bat x64`）。
