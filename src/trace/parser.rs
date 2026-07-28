use base::json::{Error as JsonError, Number, Reader, Token};

/// Borrowed key-value argument of a parsed trace event.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TraceArg<'a> {
    pub key: &'a [u8],
    pub value: &'a [u8],
    pub number: f64,
}

/// Borrowed Chrome Trace Format event emitted by `TraceParser::next_event`.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TraceEvent<'a> {
    pub name: &'a [u8],
    pub category: &'a [u8],
    pub phase: &'a [u8],
    pub color_name: &'a [u8],
    pub id: &'a [u8],
    pub timestamp: i64,
    pub duration: i64,
    pub process_id: i32,
    pub thread_id: i32,
    pub args: Vec<TraceArg<'a>>,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum State {
    #[default]
    Initial,
    LookingForTraceEvents,
    InArray,
    Complete,
    Invalid,
}

/// Streaming parser for Chrome Trace Format (JSON array or root object containing `traceEvents`).
#[derive(Default)]
pub struct TraceParser {
    buffer: Vec<u8>,
    position: usize,
    eof: bool,
    root_is_array: bool,
    state: State,
}

impl TraceParser {
    /// Creates a new `TraceParser` instance.
    pub fn new() -> Self {
        Self::default()
    }

    /// Feeds raw input chunk bytes into the parser buffer.
    /// Returns the number of discarded bytes shifted out of the internal buffer.
    pub fn feed(&mut self, bytes: &[u8], eof: bool) -> usize {
        let mut discarded = 0;
        if self.position > 128 * 1024 && self.position > self.buffer.len() * 3 / 4 {
            discarded = self.position;
            self.buffer.drain(..self.position);
            self.position = 0;
        }
        self.buffer.extend_from_slice(bytes);
        self.eof = eof;
        discarded
    }

    /// Returns `true` if parsing completed successfully.
    pub fn is_complete(&self) -> bool {
        self.state == State::Complete
    }

    /// Returns `true` if the parser encountered invalid JSON syntax.
    pub fn is_invalid(&self) -> bool {
        self.state == State::Invalid
    }

    /// Parses and returns the next trace event, or `None` if more bytes are needed or parsing is finished.
    pub fn next_event(&mut self) -> Option<TraceEvent<'_>> {
        loop {
            let mut reader = Reader::new(&self.buffer, self.position, self.eof);
            let result = match self.state {
                State::Initial => match reader.next() {
                    Ok(Token::ArrayStart) => {
                        self.root_is_array = true;
                        self.state = State::InArray;
                        Ok(None)
                    }
                    Ok(Token::ObjectStart) => {
                        self.root_is_array = false;
                        self.state = State::LookingForTraceEvents;
                        Ok(None)
                    }
                    Err(JsonError::NeedMore) => Err(JsonError::NeedMore),
                    _ => Err(JsonError::Invalid),
                },
                State::LookingForTraceEvents => {
                    Self::find_trace_events(&mut self.state, &mut reader)
                }
                State::InArray => {
                    Self::read_array_event(&mut self.state, self.root_is_array, &mut reader)
                }
                State::Complete | State::Invalid => return None,
            };
            match result {
                Ok(Some(event)) => {
                    self.position = reader.position();
                    return Some(event);
                }
                Ok(None) => {
                    self.position = reader.position();
                    continue;
                }
                Err(JsonError::NeedMore) => return None,
                Err(JsonError::Invalid) => {
                    self.state = State::Invalid;
                    return None;
                }
            }
        }
    }

    fn find_trace_events<'a>(
        state: &mut State,
        reader: &mut Reader<'a>,
    ) -> Result<Option<TraceEvent<'a>>, JsonError> {
        match reader.next()? {
            Token::ObjectEnd => {
                *state = State::Complete;
                Ok(None)
            }
            Token::Comma => Ok(None),
            Token::String(key) => {
                if reader.next()? != Token::Colon {
                    return Err(JsonError::Invalid);
                }
                let token = reader.next()?;
                if key == b"traceEvents" {
                    if token != Token::ArrayStart {
                        return Err(JsonError::Invalid);
                    }
                    *state = State::InArray;
                } else {
                    skip_value(reader, token)?;
                }
                Ok(None)
            }
            _ => Err(JsonError::Invalid),
        }
    }

    fn read_array_event<'a>(
        state: &mut State,
        root_is_array: bool,
        reader: &mut Reader<'a>,
    ) -> Result<Option<TraceEvent<'a>>, JsonError> {
        let checkpoint = reader.position();
        let mut token = reader.next()?;
        if token == Token::ArrayEnd {
            if root_is_array {
                *state = State::Complete;
            } else {
                *state = State::LookingForTraceEvents;
            }
            return Ok(None);
        }
        if token == Token::Comma {
            token = match reader.next() {
                Ok(token) => token,
                Err(JsonError::NeedMore) => {
                    reader.set_position(checkpoint);
                    return Err(JsonError::NeedMore);
                }
                Err(error) => return Err(error),
            };
        }
        if token != Token::ObjectStart {
            return Err(JsonError::Invalid);
        }
        match parse_event(reader) {
            Ok(event) => Ok(Some(event)),
            Err(JsonError::NeedMore) => {
                reader.set_position(checkpoint);
                Err(JsonError::NeedMore)
            }
            Err(error) => Err(error),
        }
    }
}

fn number_i64(number: Number) -> i64 {
    match number {
        Number::Integer(value) => value,
        Number::Float(value) => value as i64,
    }
}

fn number_i32(number: Number) -> i32 {
    number_i64(number).clamp(i64::from(i32::MIN), i64::from(i32::MAX)) as i32
}

fn parse_event<'a>(reader: &mut Reader<'a>) -> Result<TraceEvent<'a>, JsonError> {
    let mut event = TraceEvent::default();
    loop {
        let key = match reader.read_string() {
            Ok(k) => k,
            Err(JsonError::Invalid) => {
                if reader.expect_comma_or_object_end()? {
                    return Ok(event);
                }
                return Err(JsonError::Invalid);
            }
            Err(err) => return Err(err),
        };
        reader.expect_colon()?;
        // Length-based key dispatch and direct typed value reading (e.g. read_number/read_string)
        // bypass general Token enum allocations for known event attributes (ts, ph, cat, dur, pid, tid, name).
        match key.len() {
            2 => match key {
                b"ts" => event.timestamp = number_i64(reader.read_number()?),
                b"ph" => event.phase = reader.read_string()?,
                b"id" => {
                    let value = reader.next()?;
                    event.id = match value {
                        Token::String(value) => value,
                        Token::Number { source, .. } => source,
                        _ => return Err(JsonError::Invalid),
                    };
                }
                _ => {
                    let value = reader.next()?;
                    skip_value(reader, value)?;
                }
            },
            3 => match key {
                b"cat" => event.category = reader.read_string()?,
                b"dur" => event.duration = number_i64(reader.read_number()?),
                b"pid" => event.process_id = number_i32(reader.read_number()?),
                b"tid" => event.thread_id = number_i32(reader.read_number()?),
                _ => {
                    let value = reader.next()?;
                    skip_value(reader, value)?;
                }
            },
            4 => match key {
                b"name" => event.name = reader.read_string()?,
                b"args" => {
                    let value = reader.next()?;
                    if value != Token::ObjectStart {
                        return Err(JsonError::Invalid);
                    }
                    event.args = parse_args(reader)?;
                }
                _ => {
                    let value = reader.next()?;
                    skip_value(reader, value)?;
                }
            },
            5 => match key {
                b"cname" => event.color_name = reader.read_string()?,
                _ => {
                    let value = reader.next()?;
                    skip_value(reader, value)?;
                }
            },
            _ => {
                let value = reader.next()?;
                skip_value(reader, value)?;
            }
        }
        if reader.expect_comma_or_object_end()? {
            return Ok(event);
        }
    }
}

fn parse_args<'a>(reader: &mut Reader<'a>) -> Result<Vec<TraceArg<'a>>, JsonError> {
    let mut args = Vec::new();
    loop {
        let key = match reader.read_string() {
            Ok(k) => k,
            Err(JsonError::Invalid) => {
                if reader.expect_comma_or_object_end()? {
                    return Ok(args);
                }
                return Err(JsonError::Invalid);
            }
            Err(err) => return Err(err),
        };
        reader.expect_colon()?;
        let token_start = reader.position();
        let value = reader.next()?;
        let mut arg = TraceArg {
            key,
            ..TraceArg::default()
        };
        match value {
            Token::String(value) => arg.value = value,
            Token::Number {
                value: Number::Integer(number),
                ..
            } => arg.number = number as f64,
            Token::Number {
                value: Number::Float(number),
                ..
            } => arg.number = number,
            Token::True => arg.value = b"true",
            Token::False => arg.value = b"false",
            Token::Null => arg.value = b"null",
            Token::ObjectStart | Token::ArrayStart => {
                skip_value(reader, value)?;
                arg.value = &reader.input()[token_start..reader.position()];
            }
            _ => return Err(JsonError::Invalid),
        }
        args.push(arg);
        if reader.expect_comma_or_object_end()? {
            return Ok(args);
        }
    }
}

fn skip_value(reader: &mut Reader<'_>, first: Token<'_>) -> Result<(), JsonError> {
    let mut depth = match first {
        Token::ObjectStart | Token::ArrayStart => 1,
        _ => 0,
    };
    while depth > 0 {
        match reader.next() {
            Ok(Token::ObjectStart | Token::ArrayStart) => depth += 1,
            Ok(Token::ObjectEnd | Token::ArrayEnd) => depth -= 1,
            Ok(_) => {}
            Err(JsonError::NeedMore) => return Err(JsonError::NeedMore),
            Err(JsonError::Invalid) => {
                // Unknown fields are intentionally best-effort: advance past a
                // malformed byte while retaining structural nesting. This
                // matches the original parser's resilience without weakening
                // validation of fields that ztracing consumes.
                let next = reader.position().saturating_add(1);
                if next > reader.input().len() {
                    return Err(JsonError::Invalid);
                }
                reader.set_position(next);
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::trace::data::{EventMatcher, TraceData};

    fn parse_all(input: &[u8]) -> (TraceData, TraceParser) {
        let mut parser = TraceParser::new();
        parser.feed(input, true);
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        while let Some(event) = parser.next_event() {
            data.add_event(&event, &mut matcher);
        }
        (data, parser)
    }

    #[test]
    fn parses_root_array_incrementally() {
        let input = br#"[{"name":"A","ph":"X","ts":1,"dur":2,"pid":3,"tid":4,"args":{"x":5}}]"#;
        for split in 0..=input.len() {
            let mut parser = TraceParser::new();
            parser.feed(&input[..split], false);
            let first = parser
                .next_event()
                .map(|e| (e.name.to_vec(), e.timestamp, e.args[0].number));
            parser.feed(&input[split..], true);
            let (name, ts, num) = first.unwrap_or_else(|| {
                let e = parser.next_event().unwrap();
                (e.name.to_vec(), e.timestamp, e.args[0].number)
            });
            assert_eq!(name, b"A");
            assert_eq!(ts, 1);
            assert_eq!(num, 5.0);
            assert!(parser.next_event().is_none());
            assert!(parser.is_complete());
        }
    }

    #[test]
    fn parses_wrapped_trace() {
        let mut parser = TraceParser::new();
        parser.feed(
            br#"{"other":{"x":1},"traceEvents":[{"name":"A","ph":"X"}]}"#,
            true,
        );
        assert_eq!(parser.next_event().unwrap().name, b"A");
        assert!(parser.next_event().is_none());
        assert!(parser.is_complete());
    }

    #[test]
    fn parses_every_recognized_field_and_argument_kind() {
        let (data, parser) = parse_all(
            br#"[{"name":"event","cat":"category","ph":"X","cname":"rail_response","id":"string-id","ts":-12.75,"dur":3.5e2,"pid":-7.9,"tid":8.9,"args":{"string":"value","integer":42,"float":1.25,"yes":true,"no":false,"nothing":null,"array":[1,{"x":2}],"object":{"nested":3}}},{"id":-1.5e2}]"#,
        );
        assert!(parser.is_complete());
        assert!(!parser.is_invalid());
        assert_eq!(data.events.len(), 2);
        let event = &data.events[0];
        assert_eq!(data.string(event.name), b"event");
        assert_eq!(data.string(event.category), b"category");
        assert_eq!(data.string(event.phase), b"X");
        assert_eq!(data.string(event.color_name), b"rail_response");
        assert_eq!(data.string(event.id), b"string-id");
        assert_eq!((event.timestamp, event.duration), (-12, 350));
        assert_eq!((event.process_id, event.thread_id), (-7, 8));
        let args = data.event_args(event);
        assert_eq!(args.len(), 8);
        assert_eq!(
            (data.string(args[0].key), data.string(args[0].value)),
            (b"string".as_slice(), b"value".as_slice())
        );
        assert_eq!(args[1].number, 42.0);
        assert_eq!(args[2].number, 1.25);
        assert_eq!(data.string(args[3].value), b"true");
        assert_eq!(data.string(args[4].value), b"false");
        assert_eq!(data.string(args[5].value), b"null");
        assert_eq!(data.string(args[6].value), br#"[1,{"x":2}]"#);
        assert_eq!(data.string(args[7].value), br#"{"nested":3}"#);
        assert_eq!(data.string(data.events[1].id), b"-1.5e2");
    }

    #[test]
    fn rejects_wrong_types_for_recognized_fields() {
        for field in [
            r#""name":1"#,
            r#""cat":false"#,
            r#""ph":null"#,
            r#""cname":[]"#,
            r#""id":true"#,
            r#""ts":"1""#,
            r#""dur":{}"#,
            r#""pid":[]"#,
            r#""tid":null"#,
            r#""args":[]"#,
        ] {
            let input = format!("[{{{field}}}]");
            let (data, parser) = parse_all(input.as_bytes());
            assert!(data.events.is_empty(), "accepted {input}");
            assert!(parser.is_invalid(), "did not reject {input}");
        }
    }

    #[test]
    fn streams_a_complex_document_one_byte_at_a_time() {
        let input = br#"{"before":{"ignored":[1,2]},"traceEvents":[{"name":"escaped\\\"name","ph":"X","ts":-12.5e+2,"args":{"nested":[true,{"x":null}]}},{"name":"second","id":123}],"after":"ignored"}"#;
        let mut parser = TraceParser::new();
        let mut data = TraceData::new();
        let mut matcher = EventMatcher::default();
        for (index, byte) in input.iter().enumerate() {
            parser.feed(std::slice::from_ref(byte), index + 1 == input.len());
            while let Some(event) = parser.next_event() {
                data.add_event(&event, &mut matcher);
            }
        }
        assert!(parser.is_complete());
        assert!(!parser.is_invalid());
        assert_eq!(data.events.len(), 2);
        assert_eq!(data.string(data.events[0].name), br#"escaped\\\"name"#);
        assert_eq!(data.events[0].timestamp, -1250);
        let args0 = data.event_args(&data.events[0]);
        assert_eq!(data.string(args0[0].value), br#"[true,{"x":null}]"#);
        assert_eq!(data.string(data.events[1].name), b"second");
        assert_eq!(data.string(data.events[1].id), b"123");
    }

    #[test]
    fn accepts_extractable_events_despite_malformed_separators_and_trailing_data() {
        let (data, parser) =
            parse_all(br#"[,{"name":"first"} {"name":"second","unknown":{"bad":x}}] trailing"#);
        assert_eq!(data.events.len(), 2);
        assert_eq!(data.string(data.events[0].name), b"first");
        assert_eq!(data.string(data.events[1].name), b"second");
        assert!(parser.is_complete());
    }

    #[test]
    fn accepts_wrapped_fields_before_and_after_trace_events() {
        let (data, parser) = parse_all(
            br#"{"before":[{"deep":true}],"traceEvents":[{"name":"event"}],"after":{"value":1}}"#,
        );
        assert_eq!(data.events.len(), 1);
        assert_eq!(data.string(data.events[0].name), b"event");
        assert!(parser.is_complete());
    }

    #[test]
    fn final_eof_rejects_every_incomplete_prefix() {
        let input = br#"{"traceEvents":[{"name":"event","args":{"x":[1,2]}}]}"#;
        for end in 0..input.len() {
            let (_, parser) = parse_all(&input[..end]);
            assert!(
                parser.is_invalid() || !parser.is_complete(),
                "accepted incomplete prefix ending at {end}"
            );
        }
    }

    fn parse(input: &[u8]) -> TraceData {
        let (data, _) = parse_all(input);
        data
    }

    #[test]
    fn basic_array() {
        let data = parse(br#"[{"name":"foo","cat":"bar","ph":"B","ts":123,"pid":1,"tid":2}]"#);
        assert_eq!(data.events.len(), 1);
        let event = &data.events[0];
        assert_eq!(
            (
                data.string(event.name),
                data.string(event.category),
                data.string(event.phase)
            ),
            (b"foo".as_slice(), b"bar".as_slice(), b"B".as_slice())
        );
        assert_eq!(
            (event.timestamp, event.process_id, event.thread_id),
            (123, 1, 2)
        );
    }

    #[test]
    fn basic_object() {
        let data = parse(br#"{"traceEvents":[{"name":"foo"}],"other":123}"#);
        assert_eq!(data.events.len(), 1);
        assert_eq!(data.string(data.events[0].name), b"foo");
    }

    #[test]
    fn streaming() {
        let mut parser = TraceParser::new();
        parser.feed(br#"[{"name":"fo"#, false);
        assert!(parser.next_event().is_none());
        parser.feed(br#"o"},{"name":"bar"}]"#, true);
        assert_eq!(parser.next_event().unwrap().name, b"foo");
        assert_eq!(parser.next_event().unwrap().name, b"bar");
        assert!(parser.next_event().is_none());
    }

    #[test]
    fn streaming_middle_of_second_event() {
        let mut parser = TraceParser::new();
        parser.feed(br#"[{"name":"foo"},{"name":"ba"#, false);
        assert_eq!(parser.next_event().unwrap().name, b"foo");
        assert!(parser.next_event().is_none());
        parser.feed(br#"r"}]"#, true);
        assert_eq!(parser.next_event().unwrap().name, b"bar");
    }

    #[test]
    fn args() {
        let data = parse(br#"[{"name":"a","args":{"url":"http://foo","id":123,"obj":{"x":1}}}]"#);
        let args = data.event_args(&data.events[0]);
        assert_eq!(args.len(), 3);
        assert_eq!(
            (data.string(args[0].key), data.string(args[0].value)),
            (b"url".as_slice(), b"http://foo".as_slice())
        );
        assert_eq!(
            (data.string(args[1].key), args[1].number),
            (b"id".as_slice(), 123.0)
        );
        assert_eq!(
            (data.string(args[2].key), data.string(args[2].value)),
            (b"obj".as_slice(), br#"{"x":1}"#.as_slice())
        );
    }

    #[test]
    fn empty() {
        assert!(parse(b"[]").events.is_empty());
        assert!(parse(br#"{"traceEvents":[]}"#).events.is_empty());
    }

    #[test]
    fn memory_leak() {
        for _ in 0..100 {
            let data = parse(br#"[{"name":"foo","args":{"x":1}},{"name":"bar"}]"#);
            assert_eq!(data.events.len(), 2);
        }
    }

    #[test]
    fn float_numbers() {
        let data = parse(
            br#"[{"name":"foo","cat":"bar","ph":"X","ts":123.45,"dur":12.34,"pid":1.0,"tid":2.0}]"#,
        );
        let event = &data.events[0];
        assert_eq!(
            (
                event.timestamp,
                event.duration,
                event.process_id,
                event.thread_id
            ),
            (123, 12, 1, 2)
        );
    }

    #[test]
    fn exponent_numbers() {
        let data = parse(br#"[{"name":"foo","ts":1e2,"dur":1e1}]"#);
        let event = &data.events[0];
        assert_eq!((event.timestamp, event.duration), (100, 10));
    }

    #[test]
    fn infinite_loop_on_invalid_char_in_skip() {
        let mut parser = TraceParser::new();
        parser.feed(br#"[{"name":"foo","unknown":{"a":x}}]"#, true);
        let event = parser.next_event();
        assert_eq!(event.map(|event| event.name), Some(b"foo".as_slice()));
        assert!(parser.next_event().is_none());
    }

    #[test]
    fn malformed_numbers() {
        assert!(parse(br#"[{"name":"foo","ts":12+34}]"#).events.is_empty());
    }

    #[test]
    fn integer_overflow() {
        let data = parse(br#"[{"name":"pos","ts":999999999999999999999999999999,"pid":99999999999},{"name":"neg","ts":-999999999999999999999999999999,"pid":-99999999999}]"#);
        assert_eq!(
            (data.events[0].timestamp, data.events[0].process_id),
            (i64::MAX, i32::MAX)
        );
        assert_eq!(
            (data.events[1].timestamp, data.events[1].process_id),
            (i64::MIN, i32::MIN)
        );
    }
}
