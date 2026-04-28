# ztracing

A high-performance, web-based replacement for the Chrome Trace Viewer (`chrome://tracing`). It provides a smooth, modern experience for analyzing large trace files directly in your browser.

## Features

- **High Performance**: Smoothly handles massive traces with millions of events.
- **Modern Interface**: Professional UI with dark and light theme support.
- **Intuitive Navigation**: Fluid pan, zoom, and event-focusing.
- **Rich Visualization**: Hierarchical thread views and counter charts.
- **Detailed Inspection**: Deep-dive into event properties and multi-selection data.
- **Fast Loading**: Efficient parsing of large and compressed (Gzip) traces.

## Getting Started

### Prerequisites

- [Bazel](https://bazel.build/)

### Build

```bash
bazel build //:ztracing
```

### Run

To use the tool locally, run:

```bash
./tools/serve.py
```

Then open `http://localhost:8000` in your browser.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
