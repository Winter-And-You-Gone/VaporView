"""EPSILON 原始轨迹查看器 — 启动脚本

启动临时 HTTP 服务器并在默认浏览器中打开 epsilon_route_viewer.html。
Leaflet 从 CDN 加载，因此需要 HTTP 协议（file:// 下 Chrome 会阻止 CDN 资源）。
按 Ctrl+C 停止服务器。
"""
import os
import sys
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path


def main() -> None:
    html_dir = Path(__file__).resolve().parent

    # 创建不输出日志的 handler
    class QuietHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(html_dir), **kwargs)

        def log_message(self, format, *args):
            pass  # 不打印请求日志

    port = 8766
    server = HTTPServer(("127.0.0.1", port), QuietHandler)
    url = f"http://127.0.0.1:{port}/epsilon_route_viewer.html"
    print(f"服务器已启动: {url}")
    print(f"按 Ctrl+C 停止服务器并关闭页面。")

    webbrowser.open(url)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器已停止。")
        server.server_close()


if __name__ == "__main__":
    main()