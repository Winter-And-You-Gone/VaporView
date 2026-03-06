---
name: "proxy-setup"
description: "Configure git proxy for network issues. Invoke when git push/fetch fails due to network timeout or TLS errors, or when user mentions network/proxy issues."
---

# Proxy Setup Skill

This skill configures git proxy settings to resolve network connectivity issues.

## When to Use

- Git push/fetch/clone fails with TLS errors
- Network timeout errors during git operations
- User mentions network connectivity problems
- User asks about proxy configuration

## Default Proxy

```
http://192.168.55.100:7890
```

## Commands

### Set Proxy

```bash
git config --global http.proxy http://192.168.55.100:7890
git config --global https.proxy http://192.168.55.100:7890
```

### Unset Proxy

```bash
git config --global --unset http.proxy
git config --global --unset https.proxy
```

### Check Current Proxy Settings

```bash
git config --global --get http.proxy
git config --global --get https.proxy
```

## Usage

When network issues occur during git operations:

1. Set the proxy using the commands above
2. Retry the failed git operation
3. If successful, the proxy will remain configured for future operations

## Notes

- The proxy setting is global and will affect all git repositories
- If the proxy is no longer needed, unset it to avoid potential issues
- The default proxy address can be changed if a different proxy is available
