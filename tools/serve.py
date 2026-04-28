#!/usr/bin/env python3
import http.server
import os
import sys
import argparse

def run(port=8000, directory='bazel-bin/ztracing'):
    if not os.path.exists(directory):
        print(f"Error: Directory '{directory}' not found. Did you run 'bazel build //:ztracing'?")
        sys.exit(1)

    os.chdir(directory)
    
    # Ensure .wasm files are served with the correct MIME type
    http.server.SimpleHTTPRequestHandler.extensions_map.update({
        '.wasm': 'application/wasm',
    })

    server_address = ('', port)
    handler_class = http.server.SimpleHTTPRequestHandler
    
    httpd = http.server.HTTPServer(server_address, handler_class)
    
    print(f"Serving ztracing at http://localhost:{port}")
    print(f"Directory: {os.getcwd()}")
    print("Note: Service Worker will handle COOP/COEP isolation.")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server.")
        sys.exit(0)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Serve ztracing.')
    parser.add_argument('--port', type=int, default=8000, help='Port to serve on (default: 8000)')
    parser.add_argument('--dir', type=str, default='bazel-bin/ztracing', help='Directory to serve (default: bazel-bin/ztracing)')
    
    args = parser.parse_args()
    run(port=args.port, directory=args.dir)
