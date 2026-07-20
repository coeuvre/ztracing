//! Validation and ownership transfer for buffers allocated through an FFI.

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BufferError {
    NegativeSize,
    NullData,
    EmptyNonEof,
    NonNullEmptyData,
}

/// Takes ownership of an FFI buffer allocated by Rust's global allocator.
///
/// # Safety
///
/// For a positive `size`, `data` must have been allocated by Rust's active
/// global allocator using the layout for exactly `size` bytes with `u8`
/// alignment. The allocation must not have been freed or transferred
/// previously. `ztracing_malloc` is one producer that satisfies this contract.
pub unsafe fn take_owned(data: *mut u8, size: i32, eof: bool) -> Result<Vec<u8>, BufferError> {
    if size < 0 {
        return Err(BufferError::NegativeSize);
    }
    if size == 0 {
        if !data.is_null() {
            return Err(BufferError::NonNullEmptyData);
        }
        return if eof {
            Ok(Vec::new())
        } else {
            Err(BufferError::EmptyNonEof)
        };
    }
    if data.is_null() {
        return Err(BufferError::NullData);
    }
    // SAFETY: upheld by this function's caller after the shape checks above.
    Ok(unsafe { Vec::from_raw_parts(data, size as usize, size as usize) })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn positive_buffer_transfers_ownership_and_contents() {
        let bytes = vec![1_u8, 2, 3, 4].into_boxed_slice();
        let size = bytes.len() as i32;
        let pointer = Box::into_raw(bytes).cast::<u8>();
        let owned = unsafe { take_owned(pointer, size, false) }.unwrap();
        assert_eq!(owned, [1, 2, 3, 4]);
    }

    #[test]
    fn null_zero_length_eof_is_accepted() {
        assert_eq!(
            unsafe { take_owned(std::ptr::null_mut(), 0, true) },
            Ok(Vec::new())
        );
    }

    #[test]
    fn invalid_pointer_size_and_eof_combinations_are_rejected() {
        assert_eq!(
            unsafe { take_owned(std::ptr::null_mut(), -1, true) },
            Err(BufferError::NegativeSize)
        );
        assert_eq!(
            unsafe { take_owned(std::ptr::null_mut(), 1, false) },
            Err(BufferError::NullData)
        );
        assert_eq!(
            unsafe { take_owned(std::ptr::null_mut(), 0, false) },
            Err(BufferError::EmptyNonEof)
        );
        assert_eq!(
            unsafe { take_owned(std::ptr::dangling_mut(), 0, true) },
            Err(BufferError::NonNullEmptyData)
        );
    }
}
