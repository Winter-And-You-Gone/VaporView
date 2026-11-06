# 业务日志调用位置

项目使用 C++17；在支持调用位置内建函数的 MSVC、Clang 和 GCC 上，直接调用现有 API 即可记录调用点，无需宏或手写行号：

```cpp
logService.publish(LogLevel::Error, QStringLiteral("SkyCore"),
                   QStringLiteral("device.connection"), QStringLiteral("设备连接失败。"),
                   {{QStringLiteral("error_code"), QStringLiteral("CONNECT_FAILED")}});
```

权威位置保存在日志对象的顶层 `source_location` 对象的 `file`、`function`、`line` 中，与业务 `fields` 独立；Qt 消息采用相同语义。原 `fields.file`、`fields.function`、`fields.line` 仅在没有同名业务字段时继续填充，供旧消费端使用；位置判断应以顶层对象为准，即使业务本身有 `fields.source_location` 也不会相互覆盖。路径和函数名由编译器提供，可能是绝对路径或编译器特定格式。通过 `publish(LogRecord)` 转发其他进程记录时保留原进程的位置；在包装函数中发布时默认位置指向包装函数，封装层应显式转发调用位置。`reportUserIssue` 已自动转发调用位置。较旧且没有内建函数的编译器保留旧行为，不生成位置。

PPK 后处理每次启动分配独立 `correlation_id`，随任务参数传给工作线程，处理开始、RINEX 生成、进度、完成或失败日志使用相同 ID。旧的 `PpkProcessor::process` 调用未提供 ID 时会自动创建；现有 PPK 日志约定将待处理 Session 的目录写入 `session_id`。进度按 10% 桶记录，避免逐次回调刷屏。
