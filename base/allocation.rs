//! Process-wide allocation accounting.

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

static LIVE_BYTES: AtomicUsize = AtomicUsize::new(0);
const FOREIGN_ALIGNMENT: usize = 16;

/// Counts every live Rust heap allocation in the process when registered by a
/// binary or test crate:
///
/// ```ignore
/// use base::allocation::CountingAllocator;
///
/// #[global_allocator]
/// static ALLOCATOR: CountingAllocator = CountingAllocator;
/// ```
pub struct CountingAllocator;

impl CountingAllocator {
    pub fn live_bytes() -> usize {
        LIVE_BYTES.load(Ordering::Relaxed)
    }

    /// Allocates a buffer for a foreign interface that does not supply its layout
    /// when freeing it (notably Dear ImGui).
    pub unsafe fn foreign_alloc(size: usize) -> *mut u8 {
        let payload = Layout::from_size_align(size, FOREIGN_ALIGNMENT)
            .expect("foreign allocation layout overflow");
        let (layout, offset) = Layout::new::<ForeignHeader>()
            .extend(payload)
            .expect("foreign allocation layout overflow");
        // SAFETY: `layout` is valid and owned by this function on success.
        let base = unsafe { System.alloc(layout) };
        if base.is_null() {
            return base;
        }
        // SAFETY: the allocation begins with room for this header.
        unsafe { base.cast::<ForeignHeader>().write(ForeignHeader { size }) };
        LIVE_BYTES.fetch_add(size, Ordering::Relaxed);
        // SAFETY: `offset` identifies the aligned payload within the allocation.
        unsafe { base.add(offset) }
    }

    /// Frees a pointer returned by [`Self::foreign_alloc`].
    pub unsafe fn foreign_free(pointer: *mut u8) {
        if pointer.is_null() {
            return;
        }
        let (_, offset) = Layout::new::<ForeignHeader>()
            .extend(
                Layout::from_size_align(0, FOREIGN_ALIGNMENT)
                    .expect("foreign allocation alignment is valid"),
            )
            .expect("foreign allocation layout is valid");
        // SAFETY: callers promise this pointer came from `foreign_alloc`.
        let base = unsafe { pointer.sub(offset) };
        // SAFETY: `foreign_alloc` initialized this header.
        let header = unsafe { &*base.cast::<ForeignHeader>() };
        let payload = Layout::from_size_align(header.size, FOREIGN_ALIGNMENT)
            .expect("stored foreign layout is valid");
        let (layout, expected_offset) = Layout::new::<ForeignHeader>()
            .extend(payload)
            .expect("stored foreign layout is valid");
        debug_assert_eq!(offset, expected_offset);
        LIVE_BYTES.fetch_sub(header.size, Ordering::Relaxed);
        // SAFETY: the pointer/layout pair matches the allocation.
        unsafe { System.dealloc(base, layout) };
    }
}

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // SAFETY: the caller supplies a valid allocation layout.
        let pointer = unsafe { System.alloc(layout) };
        if !pointer.is_null() {
            LIVE_BYTES.fetch_add(layout.size(), Ordering::Relaxed);
        }
        pointer
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        // SAFETY: the caller supplies a valid allocation layout.
        let pointer = unsafe { System.alloc_zeroed(layout) };
        if !pointer.is_null() {
            LIVE_BYTES.fetch_add(layout.size(), Ordering::Relaxed);
        }
        pointer
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        LIVE_BYTES.fetch_sub(layout.size(), Ordering::Relaxed);
        // SAFETY: the pointer/layout pair came from `System` above.
        unsafe { System.dealloc(pointer, layout) };
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        // SAFETY: the pointer/layout pair is live and `new_size` is supplied by Rust.
        let new_pointer = unsafe { System.realloc(pointer, layout, new_size) };
        if new_pointer.is_null() {
            return new_pointer;
        }
        if new_size >= layout.size() {
            LIVE_BYTES.fetch_add(new_size - layout.size(), Ordering::Relaxed);
        } else {
            LIVE_BYTES.fetch_sub(layout.size() - new_size, Ordering::Relaxed);
        }
        new_pointer
    }
}

#[repr(C)]
struct ForeignHeader {
    size: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[global_allocator]
    static TEST_ALLOCATOR: CountingAllocator = CountingAllocator;

    #[test]
    fn ordinary_rust_allocations_are_counted_and_released() {
        let before = CountingAllocator::live_bytes();
        let mut bytes = Vec::with_capacity(64 * 1024);
        bytes.resize(64 * 1024, 7_u8);
        let allocated = CountingAllocator::live_bytes();
        assert!(allocated >= before + 64 * 1024);
        drop(bytes);
        assert!(CountingAllocator::live_bytes() <= allocated - 64 * 1024);
    }

    #[test]
    fn foreign_allocation_is_aligned_and_released() {
        let before = CountingAllocator::live_bytes();
        // SAFETY: this test frees the returned pointer exactly once below.
        let pointer = unsafe { CountingAllocator::foreign_alloc(127) };
        assert!(!pointer.is_null());
        assert_eq!(pointer as usize % FOREIGN_ALIGNMENT, 0);
        assert!(CountingAllocator::live_bytes() >= before + 127);
        // SAFETY: `pointer` was returned by `foreign_alloc` above.
        unsafe { CountingAllocator::foreign_free(pointer) };
        assert_eq!(CountingAllocator::live_bytes(), before);
    }
}
