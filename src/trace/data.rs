use crate::string_interner::{StringId, StringInterner};

/// Persisted argument representation stored in `TraceData`.
#[derive(Clone, Debug, Default)]
pub struct PersistedArg {
    pub key: StringId,
    pub value: StringId,
    pub number: f64,
}

/// Persisted event representation stored in `TraceData`.
#[derive(Clone, Debug, Default)]
pub struct PersistedEvent {
    pub name: StringId,
    pub category: StringId,
    pub phase: StringId,
    pub color_name: StringId,
    pub id: StringId,
    pub palette_index: u8,
    pub timestamp: i64,
    pub duration: i64,
    pub process_id: i32,
    pub thread_id: i32,
    pub args_offset: u32,
    pub args_count: u32,
}

/// State matcher for pairing duration Begin (`B`/`b`) and End (`E`/`e`) trace events on-the-fly.
#[derive(Default)]
pub struct EventMatcher {
    // Optimization: Trace duration events usually stream consecutively per thread.
    // An MRU stack vector (checking index 0 first) avoids SipHash HashMap overhead.
    stacks: Vec<(u64, Vec<usize>)>,
}

impl EventMatcher {
    #[inline]
    pub(crate) fn push(&mut self, thread: u64, index: usize) {
        if let Some((t, stack)) = self.stacks.first_mut() {
            if *t == thread {
                stack.push(index);
                return;
            }
        }
        if let Some(pos) = self.stacks.iter().position(|(t, _)| *t == thread) {
            self.stacks[pos].1.push(index);
            self.stacks.swap(0, pos);
        } else {
            self.stacks.push((thread, vec![index]));
            let last = self.stacks.len() - 1;
            self.stacks.swap(0, last);
        }
    }

    #[inline]
    pub(crate) fn pop(&mut self, thread: u64) -> Option<usize> {
        if let Some((t, stack)) = self.stacks.first_mut() {
            if *t == thread {
                return stack.pop();
            }
        }
        if let Some(pos) = self.stacks.iter().position(|(t, _)| *t == thread) {
            let res = self.stacks[pos].1.pop();
            self.stacks.swap(0, pos);
            res
        } else {
            None
        }
    }
}

/// Persistent trace storage hosting interned strings, events, and arguments.
#[derive(Default)]
pub struct TraceData {
    strings: StringInterner,
    pub events: Vec<PersistedEvent>,
    pub args: Vec<PersistedArg>,
    // Optimization: Dedicated 1-entry MRU caches for repetitive event attributes
    // (categories, phases, IDs, argument keys/values) to bypass StringInterner hash table lookups.
    last_name: (StringId, Vec<u8>),
    last_cat: (StringId, Vec<u8>),
    last_ph: (StringId, Vec<u8>),
    last_cname: (StringId, Vec<u8>),
    last_arg_keys: [(StringId, Vec<u8>); 4],
    last_id: (StringId, Vec<u8>),
    last_arg_vals: [(StringId, Vec<u8>); 4],
}

impl TraceData {
    /// Creates a new empty `TraceData` store.
    pub fn new() -> Self {
        Self::default()
    }

    /// Interns a byte slice into the string pool and returns its `StringId`.
    pub fn intern(&mut self, value: &[u8]) -> StringId {
        self.strings.intern(value)
    }

    fn intern_name(&mut self, value: &[u8]) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        if self.last_name.0 != StringId(0) && self.last_name.1.as_slice() == value {
            return self.last_name.0;
        }
        let id = self.strings.intern(value);
        self.last_name.0 = id;
        self.last_name.1.clear();
        self.last_name.1.extend_from_slice(value);
        id
    }

    fn intern_cat(&mut self, value: &[u8]) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        if self.last_cat.0 != StringId(0) && self.last_cat.1.as_slice() == value {
            return self.last_cat.0;
        }
        let id = self.strings.intern(value);
        self.last_cat.0 = id;
        self.last_cat.1.clear();
        self.last_cat.1.extend_from_slice(value);
        id
    }

    fn intern_ph(&mut self, value: &[u8]) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        if self.last_ph.0 != StringId(0) && self.last_ph.1.as_slice() == value {
            return self.last_ph.0;
        }
        let id = self.strings.intern(value);
        self.last_ph.0 = id;
        self.last_ph.1.clear();
        self.last_ph.1.extend_from_slice(value);
        id
    }

    fn intern_cname(&mut self, value: &[u8]) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        if self.last_cname.0 != StringId(0) && self.last_cname.1.as_slice() == value {
            return self.last_cname.0;
        }
        let id = self.strings.intern(value);
        self.last_cname.0 = id;
        self.last_cname.1.clear();
        self.last_cname.1.extend_from_slice(value);
        id
    }

    fn intern_id(&mut self, value: &[u8]) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        if self.last_id.0 != StringId(0) && self.last_id.1.as_slice() == value {
            return self.last_id.0;
        }
        let id = self.strings.intern(value);
        self.last_id.0 = id;
        self.last_id.1.clear();
        self.last_id.1.extend_from_slice(value);
        id
    }

    fn intern_arg_key(&mut self, value: &[u8], index: usize) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        let cache_idx = if index < 4 { index } else { 3 };
        if self.last_arg_keys[cache_idx].0 != StringId(0)
            && self.last_arg_keys[cache_idx].1.as_slice() == value
        {
            return self.last_arg_keys[cache_idx].0;
        }
        let id = self.strings.intern(value);
        self.last_arg_keys[cache_idx].0 = id;
        self.last_arg_keys[cache_idx].1.clear();
        self.last_arg_keys[cache_idx].1.extend_from_slice(value);
        id
    }

    fn intern_arg_val(&mut self, value: &[u8], index: usize) -> StringId {
        if value.is_empty() {
            return StringId(0);
        }
        let cache_idx = if index < 4 { index } else { 3 };
        if self.last_arg_vals[cache_idx].0 != StringId(0)
            && self.last_arg_vals[cache_idx].1.as_slice() == value
        {
            return self.last_arg_vals[cache_idx].0;
        }
        let id = self.strings.intern(value);
        self.last_arg_vals[cache_idx].0 = id;
        self.last_arg_vals[cache_idx].1.clear();
        self.last_arg_vals[cache_idx].1.extend_from_slice(value);
        id
    }

    /// Finds an existing string in the pool without interning it, returning `StringId(0)` if missing.
    pub fn find(&self, value: &[u8]) -> StringId {
        self.strings.find(value)
    }

    /// Resolves a `StringId` to its underlying byte slice.
    pub fn string(&self, reference: StringId) -> &[u8] {
        self.strings.get(reference)
    }

    /// Resolves a `StringId` to a lossy UTF-8 string view.
    pub fn string_lossy(&self, reference: StringId) -> std::borrow::Cow<'_, str> {
        String::from_utf8_lossy(self.string(reference))
    }

    pub(crate) fn string_hash(&self, reference: StringId) -> u32 {
        self.strings.hash(reference)
    }

    /// Ingests a parsed trace event, pairing duration Begin/End events on-the-fly.
    pub fn add_event(
        &mut self,
        event: &crate::trace::parser::TraceEvent<'_>,
        matcher: &mut EventMatcher,
    ) {
        let phase_slice = event.phase;
        let is_begin = matches!(phase_slice, b"B" | b"b");
        let is_end = matches!(phase_slice, b"E" | b"e");
        let thread = (u64::from(event.process_id as u32) << 32) | u64::from(event.thread_id as u32);

        if is_end {
            if let Some(index) = matcher.pop(thread) {
                let duration = event
                    .timestamp
                    .saturating_sub(self.events[index].timestamp)
                    .max(0);
                self.events[index].duration = duration;
                self.merge_args(index, &event.args);
            }
            return;
        }

        let name = self.intern_name(event.name);
        let category = self.intern_cat(event.category);
        let phase = self.intern_ph(event.phase);
        let color_name = self.intern_cname(event.color_name);
        let id = self.intern_id(event.id);
        let args_offset = u32::try_from(self.args.len()).expect("too many trace arguments");
        for (i, arg) in event.args.iter().enumerate() {
            let key = self.intern_arg_key(arg.key, i);
            let value = self.intern_arg_val(arg.value, i);
            self.args.push(PersistedArg {
                key,
                value,
                number: arg.number,
            });
        }
        let args_count = u32::try_from(event.args.len()).expect("too many event arguments");
        let palette_index = self.palette_index(name, color_name);
        let persisted = PersistedEvent {
            name,
            category,
            phase,
            color_name,
            id,
            palette_index,
            timestamp: event.timestamp,
            duration: if is_begin { 0 } else { event.duration },
            process_id: event.process_id,
            thread_id: event.thread_id,
            args_offset,
            args_count,
        };
        let index = self.events.len();
        self.events.push(persisted);
        if is_begin {
            matcher.push(thread, index);
        }
    }

    fn merge_args(&mut self, event_index: usize, incoming: &[crate::trace::parser::TraceArg<'_>]) {
        if incoming.is_empty() {
            return;
        }
        let offset = self.events[event_index].args_offset as usize;
        let count = self.events[event_index].args_count as usize;
        let mut merged = self.args[offset..offset + count].to_vec();
        for (i, arg) in incoming.iter().enumerate() {
            let key = self.intern_arg_key(arg.key, i);
            let value = self.intern_arg_val(arg.value, i);
            if let Some(existing) = merged.iter_mut().find(|existing| existing.key == key) {
                existing.value = value;
                existing.number = arg.number;
            } else {
                merged.push(PersistedArg {
                    key,
                    value,
                    number: arg.number,
                });
            }
        }
        if merged.len() == count {
            self.args[offset..offset + count].clone_from_slice(&merged);
        } else {
            self.events[event_index].args_offset =
                u32::try_from(self.args.len()).expect("too many trace arguments");
            self.events[event_index].args_count =
                u32::try_from(merged.len()).expect("too many event arguments");
            self.args.extend(merged);
        }
    }

    fn palette_index(&self, name: StringId, color_name: StringId) -> u8 {
        let named = match self.string(color_name) {
            b"thread_state_running" | b"rail_idle" => Some(3),
            b"thread_state_runnable" => Some(2),
            b"thread_state_sleeping" | b"background_memory_dump" => Some(4),
            b"thread_state_uninterruptible" => Some(0),
            b"thread_state_iowait" | b"rail_load" => Some(1),
            b"rail_animation" => Some(6),
            b"rail_response" | b"light_memory_dump" => Some(5),
            b"detailed_memory_dump" => Some(7),
            _ => None,
        };
        named.unwrap_or_else(|| (self.strings.hash(name) % 8) as u8)
    }

    pub fn event_args(&self, event: &PersistedEvent) -> &[PersistedArg] {
        let start = event.args_offset as usize;
        &self.args[start..start + event.args_count as usize]
    }

    pub fn compact(&mut self) {
        self.events.shrink_to_fit();
        self.args.shrink_to_fit();
        self.strings.compact();
    }
}

#[cfg(test)]
mod tests {
    use super::{EventMatcher, TraceData};
    use crate::trace::parser::{TraceArg, TraceEvent};

    fn event<'a>(
        name: &'a [u8],
        phase: &'a [u8],
        timestamp: i64,
        duration: i64,
        process_id: i32,
        thread_id: i32,
    ) -> TraceEvent<'a> {
        TraceEvent {
            name,
            phase,
            timestamp,
            duration,
            process_id,
            thread_id,
            ..TraceEvent::default()
        }
    }

    #[test]
    fn basic() {
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        let mut input = event(b"event1", b"X", 100, 50, 1, 2);
        input.category = b"cat1";
        input.args = vec![
            TraceArg {
                key: b"key1",
                value: b"val1",
                number: 0.0,
            },
            TraceArg {
                key: b"key2",
                value: b"val2",
                number: 0.0,
            },
        ];
        data.add_event(&input, &mut matcher);
        assert_eq!(data.events.len(), 1);
        let persisted = &data.events[0];
        assert_eq!(
            (
                data.string(persisted.name),
                data.string(persisted.category),
                data.string(persisted.phase)
            ),
            (b"event1".as_slice(), b"cat1".as_slice(), b"X".as_slice())
        );
        assert_eq!(
            (
                persisted.timestamp,
                persisted.duration,
                persisted.process_id,
                persisted.thread_id,
                persisted.args_count
            ),
            (100, 50, 1, 2, 2)
        );
        let args = data.event_args(persisted);
        assert_eq!(
            (data.string(args[0].key), data.string(args[0].value)),
            (b"key1".as_slice(), b"val1".as_slice())
        );
        assert_eq!(
            (data.string(args[1].key), data.string(args[1].value)),
            (b"key2".as_slice(), b"val2".as_slice())
        );
    }

    #[test]
    fn empty_name_uses_its_fnv1a_hash() {
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        data.add_event(&event(b"", b"X", 0, 0, 0, 0), &mut matcher);
        assert_eq!(data.events[0].palette_index, 5);
    }

    #[test]
    fn begin_end_events_basic() {
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        data.add_event(&event(b"event1", b"B", 100, 0, 1, 2), &mut matcher);
        assert_eq!(data.events[0].duration, 0);
        data.add_event(&event(b"", b"E", 150, 0, 1, 2), &mut matcher);
        assert_eq!(data.events.len(), 1);
        assert_eq!(data.events[0].duration, 50);
    }

    #[test]
    fn begin_end_events_nested_and_thread_isolated() {
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        for input in [
            event(b"parent", b"B", 100, 0, 1, 1),
            event(b"other", b"B", 110, 0, 1, 2),
            event(b"child", b"B", 120, 0, 1, 1),
        ] {
            data.add_event(&input, &mut matcher)
        }
        data.add_event(&event(b"", b"E", 130, 0, 1, 1), &mut matcher);
        assert_eq!((data.events[2].duration, data.events[0].duration), (10, 0));
        data.add_event(&event(b"", b"E", 140, 0, 1, 2), &mut matcher);
        assert_eq!(data.events[1].duration, 30);
        data.add_event(&event(b"", b"E", 150, 0, 1, 1), &mut matcher);
        assert_eq!(data.events[0].duration, 50);
    }

    #[test]
    fn begin_end_events_args_merging() {
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        let mut begin = event(b"ev", b"B", 100, 0, 1, 1);
        begin.args = vec![
            TraceArg {
                key: b"arg1",
                value: b"val1",
                number: 0.0,
            },
            TraceArg {
                key: b"arg2",
                value: b"",
                number: 42.0,
            },
        ];
        data.add_event(&begin, &mut matcher);
        let mut end = event(b"", b"E", 200, 0, 1, 1);
        end.args = vec![
            TraceArg {
                key: b"arg2",
                value: b"",
                number: 99.0,
            },
            TraceArg {
                key: b"arg3",
                value: b"val3",
                number: 0.0,
            },
        ];
        data.add_event(&end, &mut matcher);
        let persisted = &data.events[0];
        assert_eq!(persisted.args_count, 3);
        let args = data.event_args(persisted);
        assert_eq!(
            (data.string(args[0].key), data.string(args[0].value)),
            (b"arg1".as_slice(), b"val1".as_slice())
        );
        assert_eq!(
            (data.string(args[1].key), args[1].number),
            (b"arg2".as_slice(), 99.0)
        );
        assert_eq!(
            (data.string(args[2].key), data.string(args[2].value)),
            (b"arg3".as_slice(), b"val3".as_slice())
        );
    }

    #[test]
    fn duplicate_new_end_argument_uses_last_value() {
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        data.add_event(&event(b"ev", b"B", 100, 0, 1, 1), &mut matcher);
        let mut end = event(b"", b"E", 200, 0, 1, 1);
        end.args = vec![
            TraceArg {
                key: b"duplicate",
                value: b"first",
                number: 0.0,
            },
            TraceArg {
                key: b"duplicate",
                value: b"second",
                number: 0.0,
            },
        ];
        data.add_event(&end, &mut matcher);

        let args = data.event_args(&data.events[0]);
        assert_eq!(args.len(), 1);
        assert_eq!(data.string(args[0].key), b"duplicate");
        assert_eq!(data.string(args[0].value), b"second");
    }
}
