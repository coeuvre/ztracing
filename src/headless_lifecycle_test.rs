use base::allocation::CountingAllocator;
use ztracing::headless::HeadlessApp;

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

fn render_cycle() {
    let mut headless = HeadlessApp::create(64, 64).expect("headless application");
    headless.update();
}

#[test]
fn repeated_headless_lifecycle_returns_allocations_to_baseline() {
    // Warm up process-wide logging and graphics-driver state that intentionally
    // survives for the duration of the test process.
    render_cycle();
    let baseline = CountingAllocator::live_bytes();

    render_cycle();

    assert_eq!(CountingAllocator::live_bytes(), baseline);
}
