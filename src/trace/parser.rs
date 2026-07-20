use base::json::{Error as JsonError, Number, Reader, Token};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TraceArg {
    pub key: Vec<u8>,
    pub value: Vec<u8>,
    pub number: f64,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TraceEvent {
    pub name: Vec<u8>,
    pub category: Vec<u8>,
    pub phase: Vec<u8>,
    pub color_name: Vec<u8>,
    pub id: Vec<u8>,
    pub timestamp: i64,
    pub duration: i64,
    pub process_id: i32,
    pub thread_id: i32,
    pub args: Vec<TraceArg>,
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

#[derive(Default)]
pub struct TraceParser {
    buffer: Vec<u8>,
    position: usize,
    eof: bool,
    root_is_array: bool,
    state: State,
}

impl TraceParser {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn feed(&mut self, bytes: &[u8], eof: bool) -> usize {
        let mut discarded = 0;
        if self.position > 0 && self.position > self.buffer.len() / 2 {
            discarded = self.position;
            self.buffer.drain(..self.position);
            self.position = 0;
        }
        self.buffer.extend_from_slice(bytes);
        self.eof = eof;
        discarded
    }

    pub fn is_complete(&self) -> bool {
        self.state == State::Complete
    }

    pub fn is_invalid(&self) -> bool {
        self.state == State::Invalid
    }

    pub fn next_event(&mut self) -> Option<TraceEvent> {
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

    fn find_trace_events(
        state: &mut State,
        reader: &mut Reader<'_>,
    ) -> Result<Option<TraceEvent>, JsonError> {
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

    fn read_array_event(
        state: &mut State,
        root_is_array: bool,
        reader: &mut Reader<'_>,
    ) -> Result<Option<TraceEvent>, JsonError> {
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

fn parse_event(reader: &mut Reader<'_>) -> Result<TraceEvent, JsonError> {
    let mut event = TraceEvent::default();
    loop {
        let token = reader.next()?;
        if token == Token::ObjectEnd {
            return Ok(event);
        }
        let Token::String(key) = token else {
            return Err(JsonError::Invalid);
        };
        if reader.next()? != Token::Colon {
            return Err(JsonError::Invalid);
        }
        let value = reader.next()?;
        match key {
            b"name" => event.name = expect_string(value)?.to_vec(),
            b"cat" => event.category = expect_string(value)?.to_vec(),
            b"ph" => event.phase = expect_string(value)?.to_vec(),
            b"cname" => event.color_name = expect_string(value)?.to_vec(),
            b"ts" => event.timestamp = expect_number(value).map(number_i64)?,
            b"dur" => event.duration = expect_number(value).map(number_i64)?,
            b"pid" => event.process_id = expect_number(value).map(number_i32)?,
            b"tid" => event.thread_id = expect_number(value).map(number_i32)?,
            b"id" => {
                event.id = match value {
                    Token::String(value) => value.to_vec(),
                    Token::Number { source, .. } => source.to_vec(),
                    _ => return Err(JsonError::Invalid),
                }
            }
            b"args" => {
                if value != Token::ObjectStart {
                    return Err(JsonError::Invalid);
                }
                event.args = parse_args(reader)?;
            }
            _ => skip_value(reader, value)?,
        }
        match reader.next()? {
            Token::Comma => continue,
            Token::ObjectEnd => return Ok(event),
            _ => return Err(JsonError::Invalid),
        }
    }
}

fn parse_args(reader: &mut Reader<'_>) -> Result<Vec<TraceArg>, JsonError> {
    let mut args = Vec::new();
    loop {
        let token = reader.next()?;
        if token == Token::ObjectEnd {
            return Ok(args);
        }
        let Token::String(key) = token else {
            return Err(JsonError::Invalid);
        };
        if reader.next()? != Token::Colon {
            return Err(JsonError::Invalid);
        }
        let token_start = reader.position();
        let value = reader.next()?;
        let mut arg = TraceArg {
            key: key.to_vec(),
            ..TraceArg::default()
        };
        match value {
            Token::String(value) => arg.value = value.to_vec(),
            Token::Number {
                value: Number::Integer(number),
                ..
            } => arg.number = number as f64,
            Token::Number {
                value: Number::Float(number),
                ..
            } => arg.number = number,
            Token::True => arg.value = b"true".to_vec(),
            Token::False => arg.value = b"false".to_vec(),
            Token::Null => arg.value = b"null".to_vec(),
            Token::ObjectStart | Token::ArrayStart => {
                skip_value(reader, value)?;
                arg.value = reader.input()[token_start..reader.position()].to_vec();
            }
            _ => return Err(JsonError::Invalid),
        }
        args.push(arg);
        match reader.next()? {
            Token::Comma => continue,
            Token::ObjectEnd => return Ok(args),
            _ => return Err(JsonError::Invalid),
        }
    }
}

fn expect_string(token: Token<'_>) -> Result<&[u8], JsonError> {
    if let Token::String(value) = token {
        Ok(value)
    } else {
        Err(JsonError::Invalid)
    }
}

fn expect_number(token: Token<'_>) -> Result<Number, JsonError> {
    if let Token::Number { value, .. } = token {
        Ok(value)
    } else {
        Err(JsonError::Invalid)
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

    fn parse_all(input: &[u8]) -> (Vec<TraceEvent>, TraceParser) {
        let mut parser = TraceParser::new();
        parser.feed(input, true);
        let events = std::iter::from_fn(|| parser.next_event()).collect();
        (events, parser)
    }

    #[test]
    fn parses_root_array_incrementally() {
        let input = br#"[{"name":"A","ph":"X","ts":1,"dur":2,"pid":3,"tid":4,"args":{"x":5}}]"#;
        for split in 0..=input.len() {
            let mut parser = TraceParser::new();
            parser.feed(&input[..split], false);
            let first = parser.next_event();
            parser.feed(&input[split..], true);
            let event = first.or_else(|| parser.next_event()).unwrap();
            assert_eq!(event.name, b"A");
            assert_eq!(event.timestamp, 1);
            assert_eq!(event.args[0].number, 5.0);
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
        let (events, parser) = parse_all(
            br#"[{"name":"event","cat":"category","ph":"X","cname":"rail_response","id":"string-id","ts":-12.75,"dur":3.5e2,"pid":-7.9,"tid":8.9,"args":{"string":"value","integer":42,"float":1.25,"yes":true,"no":false,"nothing":null,"array":[1,{"x":2}],"object":{"nested":3}}},{"id":-1.5e2}]"#,
        );
        assert!(parser.is_complete());
        assert!(!parser.is_invalid());
        assert_eq!(events.len(), 2);
        let event = &events[0];
        assert_eq!(event.name, b"event");
        assert_eq!(event.category, b"category");
        assert_eq!(event.phase, b"X");
        assert_eq!(event.color_name, b"rail_response");
        assert_eq!(event.id, b"string-id");
        assert_eq!((event.timestamp, event.duration), (-12, 350));
        assert_eq!((event.process_id, event.thread_id), (-7, 8));
        assert_eq!(event.args.len(), 8);
        assert_eq!(
            (&event.args[0].key[..], &event.args[0].value[..]),
            (b"string".as_slice(), b"value".as_slice())
        );
        assert_eq!(event.args[1].number, 42.0);
        assert_eq!(event.args[2].number, 1.25);
        assert_eq!(event.args[3].value, b"true");
        assert_eq!(event.args[4].value, b"false");
        assert_eq!(event.args[5].value, b"null");
        assert_eq!(event.args[6].value, br#"[1,{"x":2}]"#);
        assert_eq!(event.args[7].value, br#"{"nested":3}"#);
        assert_eq!(events[1].id, b"-1.5e2");
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
            let (events, parser) = parse_all(input.as_bytes());
            assert!(events.is_empty(), "accepted {input}");
            assert!(parser.is_invalid(), "did not reject {input}");
        }
    }

    #[test]
    fn streams_a_complex_document_one_byte_at_a_time() {
        let input = br#"{"before":{"ignored":[1,2]},"traceEvents":[{"name":"escaped\\\"name","ph":"X","ts":-12.5e+2,"args":{"nested":[true,{"x":null}]}},{"name":"second","id":123}],"after":"ignored"}"#;
        let mut parser = TraceParser::new();
        let mut events = Vec::new();
        for (index, byte) in input.iter().enumerate() {
            parser.feed(std::slice::from_ref(byte), index + 1 == input.len());
            while let Some(event) = parser.next_event() {
                events.push(event);
            }
        }
        assert!(parser.is_complete());
        assert!(!parser.is_invalid());
        assert_eq!(events.len(), 2);
        assert_eq!(events[0].name, br#"escaped\\\"name"#);
        assert_eq!(events[0].timestamp, -1250);
        assert_eq!(events[0].args[0].value, br#"[true,{"x":null}]"#);
        assert_eq!(events[1].name, b"second");
        assert_eq!(events[1].id, b"123");
    }

    #[test]
    fn accepts_extractable_events_despite_malformed_separators_and_trailing_data() {
        let (events, parser) =
            parse_all(br#"[,{"name":"first"} {"name":"second","unknown":{"bad":x}}] trailing"#);
        assert_eq!(events.len(), 2);
        assert_eq!(events[0].name, b"first");
        assert_eq!(events[1].name, b"second");
        assert!(parser.is_complete());
    }

    #[test]
    fn accepts_wrapped_fields_before_and_after_trace_events() {
        let (events, parser) = parse_all(
            br#"{"before":[{"deep":true}],"traceEvents":[{"name":"event"}],"after":{"value":1}}"#,
        );
        assert_eq!(events.len(), 1);
        assert_eq!(events[0].name, b"event");
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

    fn parse(input: &[u8]) -> Vec<TraceEvent> {
        let mut parser = TraceParser::new();
        parser.feed(input, true);
        std::iter::from_fn(|| parser.next_event()).collect()
    }

    #[test]
    fn basic_array() {
        let events = parse(br#"[{"name":"foo","cat":"bar","ph":"B","ts":123,"pid":1,"tid":2}]"#);
        assert_eq!(events.len(), 1);
        let event = &events[0];
        assert_eq!(
            (&event.name[..], &event.category[..], &event.phase[..]),
            (b"foo".as_slice(), b"bar".as_slice(), b"B".as_slice())
        );
        assert_eq!(
            (event.timestamp, event.process_id, event.thread_id),
            (123, 1, 2)
        );
    }

    #[test]
    fn basic_object() {
        let events = parse(br#"{"traceEvents":[{"name":"foo"}],"other":123}"#);
        assert_eq!(events.len(), 1);
        assert_eq!(events[0].name, b"foo");
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
        let events = parse(br#"[{"name":"a","args":{"url":"http://foo","id":123,"obj":{"x":1}}}]"#);
        let args = &events[0].args;
        assert_eq!(args.len(), 3);
        assert_eq!(
            (&args[0].key[..], &args[0].value[..]),
            (b"url".as_slice(), b"http://foo".as_slice())
        );
        assert_eq!(
            (&args[1].key[..], args[1].number),
            (b"id".as_slice(), 123.0)
        );
        assert_eq!(
            (&args[2].key[..], &args[2].value[..]),
            (b"obj".as_slice(), br#"{"x":1}"#.as_slice())
        );
    }

    #[test]
    fn empty() {
        assert!(parse(b"[]").is_empty());
        assert!(parse(br#"{"traceEvents":[]}"#).is_empty());
    }

    #[test]
    fn memory_leak() {
        for _ in 0..100 {
            let events = parse(br#"[{"name":"foo","args":{"x":1}},{"name":"bar"}]"#);
            assert_eq!(events.len(), 2);
        }
    }

    #[test]
    fn float_numbers() {
        let event = parse(
            br#"[{"name":"foo","cat":"bar","ph":"B","ts":123.45,"dur":12.34,"pid":1.0,"tid":2.0}]"#,
        )
        .remove(0);
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
        let event = parse(br#"[{"name":"foo","ts":1e2,"dur":1e1}]"#).remove(0);
        assert_eq!((event.timestamp, event.duration), (100, 10));
    }

    #[test]
    fn infinite_loop_on_invalid_char_in_skip() {
        let mut parser = TraceParser::new();
        parser.feed(br#"[{"name":"foo","unknown":{"a":x}}]"#, true);
        let event = parser.next_event();
        assert_eq!(event.map(|event| event.name), Some(b"foo".to_vec()));
        assert!(parser.next_event().is_none());
    }

    #[test]
    fn malformed_numbers() {
        assert!(parse(br#"[{"name":"foo","ts":12+34}]"#).is_empty());
    }

    #[test]
    fn integer_overflow() {
        let events = parse(br#"[{"name":"pos","ts":999999999999999999999999999999,"pid":99999999999},{"name":"neg","ts":-999999999999999999999999999999,"pid":-99999999999}]"#);
        assert_eq!(
            (events[0].timestamp, events[0].process_id),
            (i64::MAX, i32::MAX)
        );
        assert_eq!(
            (events[1].timestamp, events[1].process_id),
            (i64::MIN, i32::MIN)
        );
    }
}
