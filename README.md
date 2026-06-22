# ztracing

A high-performance, web-based replacement for the Chrome Trace Viewer (`chrome://tracing`). It provides a smooth, modern experience for analyzing large trace files directly in your browser.

## Features

- **High Performance**: Smoothly handles massive traces with millions of events.
- **Modern Interface**: Professional UI with dark and light theme support.
- **Intuitive Navigation**: Fluid pan, zoom, and event-focusing.
- **Rich Visualization**: Hierarchical thread views and counter charts.
- **Detailed Inspection**: Deep-dive into event properties and multi-selection data.
- **Fast Loading**: Efficient parsing of large and compressed (Gzip) traces.
- **Native CLI Utility**: Lightning-fast, zero-dependency command-line tool for headless trace queries and automated performance diagnostics.
- **AI Agent Skill**: Seamless integration with LLM-based coding assistants via a portable workspace skill for automated diagnostics.

## Getting Started

### Prerequisites

- [Bazel](https://bazel.build/)

### Build the Web App

```bash
bazel build //:ztracing
```

### Run the Web App

To use the web tool locally, run:

```bash
./tools/serve.py
```

Then open `http://localhost:8000` in your browser.

### Build & Run the Native CLI

The project includes a high-performance, native command-line utility for headless trace query, performance diagnostics, and automated analysis.

#### Build the CLI
```bash
bazel build //src:ztracing
```
The compiled binary will be available at `bazel-bin/src/ztracing`.

#### Subcommands Available
*   `summary <trace_file>`: Print high-level trace metadata (event count, tracks, duration).
*   `tracks <trace_file>`: List all organized tracks and maximum stack depths.
*   `inspect <trace_file> --track <name> --ts <ts_us>`: Retrieve detailed event parameters (parent, children, exclusive self-time, and arguments) at a timeline coordinate.
*   `query <trace_file> [filters]`: Search and extract matching events chronologically.
*   `heatmap <trace_file>`: Precompute the 2D activity minimap grid.
*   `histogram <trace_file> [filters]`: Calculate linear or logarithmic duration distribution buckets.

All subcommands support a global `--pretty` flag for formatted JSON output.

---

## AI Agent Integration

The repository contains a portable, workspace-importable **Agent Skill** (`trace-analyzer`) that enables LLM-based coding assistants to perform automated trace diagnostics and performance troubleshooting.

The skill is located at [.agents/skills/trace-analyzer/SKILL.md](.agents/skills/trace-analyzer/SKILL.md). When imported, agents can automatically run the CLI to isolate bottlenecks, calculate event distributions, and inspect call hierarchies on your traces.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
