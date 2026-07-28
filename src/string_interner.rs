#[repr(transparent)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct StringId(pub u32);

#[derive(Clone, Copy, Default)]
struct StringEntry {
    offset: u32,
    length: u32,
    hash: u32,
}

#[derive(Clone, Copy, Default)]
struct LookupEntry {
    reference: StringId,
    hash: u32,
}

#[derive(Default)]
pub(crate) struct StringInterner {
    buffer: Vec<u8>,
    entries: Vec<StringEntry>,
    lookup: Vec<LookupEntry>,
    lookup_size: usize,
    last_hash: u32,
    last_id: StringId,
}

impl StringInterner {
    pub(crate) fn intern(&mut self, value: &[u8]) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        let hash = fnv1a(value);
        if hash == self.last_hash && self.last_id != StringId(0) && self.get(self.last_id) == value {
            return self.last_id;
        }
        if self.lookup.is_empty() {
            self.resize_lookup(16);
        }
        let mut slot = hash as usize & (self.lookup.len() - 1);
        loop {
            let entry = self.lookup[slot];
            if entry.reference == StringId(0) {
                break;
            }
            if entry.hash == hash && self.get(entry.reference) == value {
                self.last_hash = hash;
                self.last_id = entry.reference;
                return entry.reference;
            }
            slot = (slot + 1) & (self.lookup.len() - 1);
        }

        let offset = u32::try_from(self.buffer.len()).expect("interned string pool too large");
        let length = u32::try_from(value.len()).expect("interned string too large");
        let reference =
            StringId(u32::try_from(self.entries.len() + 1).expect("too many interned strings"));
        self.buffer.extend_from_slice(value);
        self.entries.push(StringEntry {
            offset,
            length,
            hash,
        });
        self.lookup[slot] = LookupEntry { reference, hash };
        self.lookup_size += 1;
        if self.lookup_size * 2 > self.lookup.len() {
            self.resize_lookup(self.lookup.len() * 2);
        }
        self.last_hash = hash;
        self.last_id = reference;
        reference
    }

    pub(crate) fn find(&self, value: &[u8]) -> StringId {
        if value.is_empty() || self.lookup.is_empty() {
            return StringId(0);
        }
        let hash = fnv1a(value);
        let mut slot = hash as usize & (self.lookup.len() - 1);
        loop {
            let entry = self.lookup[slot];
            if entry.reference == StringId(0) {
                return StringId(0);
            }
            if entry.hash == hash && self.get(entry.reference) == value {
                return entry.reference;
            }
            slot = (slot + 1) & (self.lookup.len() - 1);
        }
    }

    pub(crate) fn get(&self, reference: StringId) -> &[u8] {
        reference
            .0
            .checked_sub(1)
            .and_then(|index| self.entries.get(index as usize))
            .and_then(|entry| {
                let start = entry.offset as usize;
                self.buffer.get(start..start + entry.length as usize)
            })
            .unwrap_or_default()
    }

    pub(crate) fn hash(&self, reference: StringId) -> u32 {
        if reference == StringId(0) {
            return fnv1a(b"");
        }
        reference
            .0
            .checked_sub(1)
            .and_then(|index| self.entries.get(index as usize))
            .map_or(0, |entry| entry.hash)
    }

    pub(crate) fn compact(&mut self) {
        self.buffer.shrink_to_fit();
        self.entries.shrink_to_fit();
    }

    fn resize_lookup(&mut self, requested_capacity: usize) {
        let capacity = requested_capacity.max(16).next_power_of_two();
        let mut replacement = vec![LookupEntry::default(); capacity];
        for entry in self
            .lookup
            .iter()
            .copied()
            .filter(|entry| entry.reference != StringId(0))
        {
            let mut slot = entry.hash as usize & (capacity - 1);
            while replacement[slot].reference != StringId(0) {
                slot = (slot + 1) & (capacity - 1);
            }
            replacement[slot] = entry;
        }
        self.lookup = replacement;
    }
}

#[inline(always)]
fn fnv1a(bytes: &[u8]) -> u32 {
    let mut hash = 2_166_136_261_u32;
    for &byte in bytes {
        hash = (hash ^ u32::from(byte)).wrapping_mul(16_777_619);
    }
    hash
}

#[cfg(test)]
mod tests {
    use super::{StringId, StringInterner};

    #[test]
    fn deduplicates_strings() {
        let mut strings = StringInterner::default();
        let first = strings.intern(b"foo");
        let second = strings.intern(b"bar");
        let third = strings.intern(b"foo");
        assert_eq!(first, third);
        assert_ne!(first, second);
        assert_eq!(strings.get(first), b"foo");
        assert_eq!(strings.get(second), b"bar");
    }

    #[test]
    fn stores_one_contiguous_copy_and_survives_lookup_growth() {
        let mut strings = StringInterner::default();
        let values: Vec<Vec<u8>> = (0..1000)
            .map(|index| format!("string-{index}").into_bytes())
            .collect();
        let expected_bytes: usize = values.iter().map(Vec::len).sum();
        let references: Vec<_> = values.iter().map(|value| strings.intern(value)).collect();

        assert_eq!(strings.buffer.len(), expected_bytes);
        assert_eq!(strings.entries.len(), values.len());
        for ((value, reference), expected_reference) in values.iter().zip(references).zip(1_u32..) {
            assert_eq!(reference.0, expected_reference);
            assert_eq!(strings.find(value), reference);
            assert_eq!(strings.get(reference), value);
            assert_eq!(strings.intern(value), reference);
        }
        assert_eq!(strings.buffer.len(), expected_bytes);
        assert_eq!(strings.find(b"missing"), StringId(0));
    }

    #[test]
    fn empty_and_invalid_references_resolve_to_empty() {
        let mut strings = StringInterner::default();
        assert_eq!(strings.intern(b""), StringId(0));
        assert_eq!(strings.find(b""), StringId(0));
        assert_eq!(strings.get(StringId(0)), b"");
        assert_eq!(strings.get(StringId(99)), b"");
        assert_eq!(strings.hash(StringId(0)), 2_166_136_261);
        assert_eq!(strings.hash(StringId(99)), 0);
    }
}
