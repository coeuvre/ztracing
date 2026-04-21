#!/usr/bin/env python3
import http.server
import os
import sys

class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

if __name__ == '__main__':
    port = 8000
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    directory = 'bazel-bin/ztracing'
    if not os.path.exists(directory):
        print(f"Error: Directory '{directory}' not found. Did you run 'bazel build //:ztracing'?")
        sys.exit(1)

    os.chdir(directory)
    print(f"Serving ztracing at http://localhost:{port}")
    http.server.test(HandlerClass=Handler, port=port)
