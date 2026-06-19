import json
import sys

def generate_trace(filename, num_events):
    print(f"Generating mock trace with {num_events} events to {filename}...")
    with open(filename, "w") as f:
        f.write("[\n")
        # Write metadata
        f.write('  {"name": "process_name", "ph": "M", "pid": 1, "args": {"name": "Benchmark Process"}},\n')
        f.write('  {"name": "thread_name", "ph": "M", "pid": 1, "tid": 1, "args": {"name": "Benchmark Thread 1"}},\n')
        f.write('  {"name": "thread_name", "ph": "M", "pid": 1, "tid": 2, "args": {"name": "Benchmark Thread 2"}},\n')
        
        ts = 100
        for i in range(num_events // 2):
            tid = 1 if (i % 2 == 0) else 2
            name = f"event_{i % 100}"
            # Begin event
            f.write(f'  {{"name": "{name}", "cat": "test", "ph": "B", "pid": 1, "tid": {tid}, "ts": {ts}}},\n')
            # Nested event
            f.write(f'  {{"name": "nested_{name}", "cat": "test", "ph": "B", "pid": 1, "tid": {tid}, "ts": {ts + 2}}},\n')
            f.write(f'  {{"name": "nested_{name}", "cat": "test", "ph": "E", "pid": 1, "tid": {tid}, "ts": {ts + 8}}},\n')
            # End event
            f.write(f'  {{"name": "{name}", "cat": "test", "ph": "E", "pid": 1, "tid": {tid}, "ts": {ts + 10}}},\n')
            # Counter event
            f.write(f'  {{"name": "my_counter", "ph": "C", "pid": 1, "ts": {ts}, "args": {{"value": {i % 100}}}}},\n')
            
            ts += 20
            if i % 10000 == 0:
                sys.stdout.write(".")
                sys.stdout.flush()
        
        # Write one final dummy event to close the array neatly
        f.write('  {"name": "final", "ph": "I", "pid": 1, "tid": 1, "ts": %d}\n' % ts)
        f.write("]\n")
    print(f"\nDone! Mock trace generated successfully.")

if __name__ == "__main__":
    num_events = 500000 # 500k events in total
    if len(sys.argv) > 1:
        num_events = int(sys.argv[1])
    generate_trace("mock_trace.json", num_events)
