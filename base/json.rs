//! Small byte-oriented JSON tokenizer used by the streaming trace parser.

/// Represents a parsed JSON numeric value, either an integer (`i64`) or a float (`f64`).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Number {
    Integer(i64),
    Float(f64),
}

/// Token emitted by `Reader` during byte-level JSON scanning.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Token<'a> {
    ObjectStart,
    ObjectEnd,
    ArrayStart,
    ArrayEnd,
    String(&'a [u8]),
    Number { value: Number, source: &'a [u8] },
    True,
    False,
    Null,
    Colon,
    Comma,
}

/// Error conditions returned during JSON parsing.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Error {
    /// Insufficient data in buffer; caller should feed more bytes.
    NeedMore,
    /// Invalid JSON syntax encountered.
    Invalid,
}

/// Zero-allocation, streaming JSON reader operating over an immutable byte slice.
#[derive(Clone, Copy, Debug)]
pub struct Reader<'a> {
    input: &'a [u8],
    position: usize,
    eof: bool,
}

impl<'a> Reader<'a> {
    /// Creates a new JSON reader for the provided byte slice starting at `position`.
    pub fn new(input: &'a [u8], position: usize, eof: bool) -> Self {
        Self {
            input,
            position,
            eof,
        }
    }

    /// Returns the current byte offset position within the input buffer.
    pub fn position(&self) -> usize {
        self.position
    }

    /// Sets the current byte offset position of the reader.
    pub fn set_position(&mut self, position: usize) {
        self.position = position;
    }

    /// Returns a reference to the underlying input byte slice.
    pub fn input(&self) -> &'a [u8] {
        self.input
    }

    /// Returns `true` if the reader position has reached or passed the end of the input buffer.
    pub fn is_done(&self) -> bool {
        self.position >= self.input.len()
    }

    fn incomplete(&self) -> Error {
        if self.eof {
            Error::Invalid
        } else {
            Error::NeedMore
        }
    }

    /// Advances the reader past any ASCII whitespace bytes.
    #[inline]
    pub fn skip_whitespace(&mut self) {
        // In minified JSON, >99% of characters are non-whitespace (> 0x20).
        // An inline guard turns whitespace scanning into a single fast comparison.
        if self.position < self.input.len() && self.input[self.position] > b' ' {
            return;
        }
        let rest = &self.input[self.position..];
        let count = rest
            .iter()
            .position(|&b| !matches!(b, b' ' | b'\n' | b'\r' | b'\t'))
            .unwrap_or(rest.len());
        self.position += count;
    }

    /// Scans and returns the next token from the input.
    pub fn next(&mut self) -> Result<Token<'a>, Error> {
        self.skip_whitespace();
        let Some(&byte) = self.input.get(self.position) else {
            return Err(self.incomplete());
        };
        match byte {
            b'{' => self.single(Token::ObjectStart),
            b'}' => self.single(Token::ObjectEnd),
            b'[' => self.single(Token::ArrayStart),
            b']' => self.single(Token::ArrayEnd),
            b':' => self.single(Token::Colon),
            b',' => self.single(Token::Comma),
            b'"' => self.string(),
            b't' => self.keyword(b"true", Token::True),
            b'f' => self.keyword(b"false", Token::False),
            b'n' => self.keyword(b"null", Token::Null),
            b'-' | b'0'..=b'9' => self.number(),
            _ => Err(Error::Invalid),
        }
    }

    /// Skips whitespace and expects a colon (`:`).
    #[inline]
    pub fn expect_colon(&mut self) -> Result<(), Error> {
        self.skip_whitespace();
        if self.position < self.input.len() {
            if self.input[self.position] == b':' {
                self.position += 1;
                Ok(())
            } else {
                Err(Error::Invalid)
            }
        } else {
            Err(self.incomplete())
        }
    }

    /// Skips whitespace and expects either a comma (`,`) or object end (`}`).
    /// Returns `true` if object end (`}`) was matched.
    #[inline]
    pub fn expect_comma_or_object_end(&mut self) -> Result<bool, Error> {
        self.skip_whitespace();
        if self.position < self.input.len() {
            let b = self.input[self.position];
            if b == b',' {
                self.position += 1;
                Ok(false)
            } else if b == b'}' {
                self.position += 1;
                Ok(true)
            } else {
                Err(Error::Invalid)
            }
        } else {
            Err(self.incomplete())
        }
    }

    fn single(&mut self, token: Token<'a>) -> Result<Token<'a>, Error> {
        self.position += 1;
        Ok(token)
    }

    fn keyword(&mut self, keyword: &[u8], token: Token<'a>) -> Result<Token<'a>, Error> {
        let remaining = &self.input[self.position..];
        if remaining.len() < keyword.len() {
            return Err(self.incomplete());
        }
        if !remaining.starts_with(keyword) {
            return Err(Error::Invalid);
        }
        self.position += keyword.len();
        Ok(token)
    }

    /// Fast-path string extraction that checks for leading quotes
    /// directly without general `Token` enum allocation.
    #[inline]
    pub fn read_string(&mut self) -> Result<&'a [u8], Error> {
        // Fast-path string extraction that checks for leading quotes
        // directly without general Token enum allocation.
        self.skip_whitespace();
        if self.position < self.input.len() {
            if self.input[self.position] == b'"' {
                self.string_slice()
            } else {
                Err(Error::Invalid)
            }
        } else {
            Err(self.incomplete())
        }
    }

    fn string(&mut self) -> Result<Token<'a>, Error> {
        self.string_slice().map(Token::String)
    }

    #[inline]
    fn string_slice(&mut self) -> Result<&'a [u8], Error> {
        let start = self.position + 1;
        let rest = &self.input[start..];
        let mut offset = 0;
        while offset < rest.len() {
            let rel = rest[offset..].iter().position(|&b| b == b'"' || b == b'\\');
            match rel {
                Some(p) => {
                    let pos = offset + p;
                    if rest[pos] == b'"' {
                        self.position = start + pos + 1;
                        return Ok(&rest[..pos]);
                    }
                    offset = pos + 2;
                }
                None => break,
            }
        }
        Err(self.incomplete())
    }

    /// Fast-path number extraction that parses a number without general `Token` enum allocation.
    #[inline]
    pub fn read_number(&mut self) -> Result<Number, Error> {
        self.skip_whitespace();
        let tok = self.number()?;
        if let Token::Number { value, .. } = tok {
            Ok(value)
        } else {
            Err(Error::Invalid)
        }
    }

    fn number(&mut self) -> Result<Token<'a>, Error> {
        let start = self.position;
        let mut cursor = start;
        if self.input.get(cursor) == Some(&b'-') {
            cursor += 1;
        }
        match self.input.get(cursor) {
            Some(b'0') => cursor += 1,
            Some(b'1'..=b'9') => {
                cursor += 1;
                while matches!(self.input.get(cursor), Some(b'0'..=b'9')) {
                    cursor += 1;
                }
            }
            Some(_) => return Err(Error::Invalid),
            None => return Err(self.incomplete()),
        }

        let mut is_float = false;
        if self.input.get(cursor) == Some(&b'.') {
            is_float = true;
            cursor += 1;
            let fraction_start = cursor;
            while matches!(self.input.get(cursor), Some(b'0'..=b'9')) {
                cursor += 1;
            }
            if cursor == fraction_start {
                return Err(if cursor >= self.input.len() {
                    self.incomplete()
                } else {
                    Error::Invalid
                });
            }
        }
        if matches!(self.input.get(cursor), Some(b'e' | b'E')) {
            is_float = true;
            cursor += 1;
            if matches!(self.input.get(cursor), Some(b'+' | b'-')) {
                cursor += 1;
            }
            let exponent_start = cursor;
            while matches!(self.input.get(cursor), Some(b'0'..=b'9')) {
                cursor += 1;
            }
            if cursor == exponent_start {
                return Err(if cursor >= self.input.len() {
                    self.incomplete()
                } else {
                    Error::Invalid
                });
            }
        }

        if cursor == self.input.len() && !self.eof {
            return Err(Error::NeedMore);
        }
        if let Some(&next) = self.input.get(cursor) {
            if !matches!(
                next,
                b' ' | b'\n' | b'\r' | b'\t' | b',' | b']' | b'}' | b':'
            ) {
                return Err(Error::Invalid);
            }
        }

        let source = &self.input[start..cursor];
        // SAFETY: `source` consists exclusively of ASCII numeric/sign/exponent bytes verified by the scanner above.
        let text = unsafe { std::str::from_utf8_unchecked(source) };
        let value = if is_float {
            Number::Float(text.parse().map_err(|_| Error::Invalid)?)
        } else {
            Number::Integer(parse_i64_fast(source).unwrap_or_else(|| {
                if source.first() == Some(&b'-') {
                    i64::MIN
                } else {
                    i64::MAX
                }
            }))
        };
        self.position = cursor;
        Ok(Token::Number { value, source })
    }
}

#[inline]
fn parse_i64_fast(source: &[u8]) -> Option<i64> {
    let (is_neg, digits) = match source.first()? {
        b'-' => (true, &source[1..]),
        _ => (false, source),
    };
    if digits.is_empty() {
        return None;
    }
    let mut val: i64 = 0;
    // For numbers with <= 18 digits (e.g., timestamps, process/thread IDs),
    // overflow cannot occur within i64::MAX. The unchecked loop eliminates overflow checks per digit.
    if digits.len() <= 18 {
        for &b in digits {
            val = val * 10 + i64::from(b - b'0');
        }
    } else {
        for &b in digits {
            val = val.checked_mul(10)?.checked_add(i64::from(b - b'0'))?;
        }
    }
    Some(if is_neg { -val } else { val })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic() {
        let mut reader = Reader::new(br#"{"key": [1, 2.5, true, false, null]}"#, 0, true);
        assert_eq!(reader.next(), Ok(Token::ObjectStart));
        assert_eq!(reader.next(), Ok(Token::String(b"key")));
        assert_eq!(reader.next(), Ok(Token::Colon));
        assert_eq!(reader.next(), Ok(Token::ArrayStart));
        assert_eq!(
            reader.next(),
            Ok(Token::Number {
                value: Number::Integer(1),
                source: b"1"
            })
        );
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(
            reader.next(),
            Ok(Token::Number {
                value: Number::Float(2.5),
                source: b"2.5"
            })
        );
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(reader.next(), Ok(Token::True));
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(reader.next(), Ok(Token::False));
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(reader.next(), Ok(Token::Null));
        assert_eq!(reader.next(), Ok(Token::ArrayEnd));
        assert_eq!(reader.next(), Ok(Token::ObjectEnd));
        assert_eq!(reader.next(), Err(Error::Invalid))
    }

    #[test]
    fn string_edge_cases() {
        for input in [
            br#"{"key": "unclosed}"#.as_slice(),
            br#"["hello\"#.as_slice(),
        ] {
            let mut reader = Reader::new(input, 0, true);
            loop {
                if reader.next().is_err() {
                    break;
                }
            }
            assert_eq!(reader.next(), Err(Error::Invalid))
        }
        let mut reader = Reader::new(br#"["hello\\world", "hello\"world", ""]"#, 0, true);
        assert_eq!(reader.next(), Ok(Token::ArrayStart));
        assert_eq!(reader.next(), Ok(Token::String(br#"hello\\world"#)));
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(reader.next(), Ok(Token::String(br#"hello\"world"#)));
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(reader.next(), Ok(Token::String(b"")))
    }

    #[test]
    fn number_edge_cases() {
        let mut reader = Reader::new(b"[-123.456, 1e9, 2.5e-4, 3.14e+2]", 0, true);
        assert_eq!(reader.next(), Ok(Token::ArrayStart));
        for expected in [-123.456, 1e9, 2.5e-4, 314.0] {
            let Token::Number {
                value: Number::Float(value),
                ..
            } = reader.next().unwrap()
            else {
                panic!()
            };
            assert_eq!(value, expected);
            if expected != 314.0 {
                assert_eq!(reader.next(), Ok(Token::Comma))
            }
        }
        let mut malformed = Reader::new(b"[-]", 0, true);
        assert_eq!(malformed.next(), Ok(Token::ArrayStart));
        assert_eq!(malformed.next(), Err(Error::Invalid))
    }

    #[test]
    fn literal_edge_cases() {
        for input in [b"[trud]".as_slice(), b"[falsy]", b"[@]"] {
            let mut reader = Reader::new(input, 0, true);
            assert_eq!(reader.next(), Ok(Token::ArrayStart));
            assert_eq!(reader.next(), Err(Error::Invalid))
        }
        let mut reader = Reader::new(b"[nulle]", 0, true);
        assert_eq!(reader.next(), Ok(Token::ArrayStart));
        assert_eq!(reader.next(), Ok(Token::Null));
        assert_eq!(reader.next(), Err(Error::Invalid))
    }

    #[test]
    fn whitespace() {
        let mut reader = Reader::new(b"  \n \t \r [ \n \t \r ] \n \t \r ", 0, true);
        assert_eq!(reader.next(), Ok(Token::ArrayStart));
        assert_eq!(reader.next(), Ok(Token::ArrayEnd));
        assert_eq!(reader.next(), Err(Error::Invalid))
    }

    #[test]
    fn tokenizes_values() {
        let input = br#"{"a":[-12,3.5e2,true,false,null,"x\\\"y"]}"#;
        let mut reader = Reader::new(input, 0, true);
        assert_eq!(reader.next(), Ok(Token::ObjectStart));
        assert_eq!(reader.next(), Ok(Token::String(b"a")));
        assert_eq!(reader.next(), Ok(Token::Colon));
        assert_eq!(reader.next(), Ok(Token::ArrayStart));
        assert_eq!(
            reader.next(),
            Ok(Token::Number {
                value: Number::Integer(-12),
                source: b"-12"
            })
        );
        assert_eq!(reader.next(), Ok(Token::Comma));
        assert_eq!(
            reader.next(),
            Ok(Token::Number {
                value: Number::Float(350.0),
                source: b"3.5e2"
            })
        );
    }

    #[test]
    fn reports_incomplete_string() {
        let mut reader = Reader::new(br#""abc"#, 0, false);
        assert_eq!(reader.next(), Err(Error::NeedMore));
    }

    #[test]
    fn rejects_leading_zero_numbers() {
        for input in [b"01]".as_slice(), b"-01]", b"00]"] {
            let mut reader = Reader::new(input, 0, true);
            assert_eq!(reader.next(), Err(Error::Invalid));
            assert_eq!(reader.position(), 0);
        }
    }

    #[test]
    fn reports_partial_literals_as_need_more() {
        for literal in [b"true".as_slice(), b"false", b"null"] {
            for split in 1..literal.len() {
                let mut reader = Reader::new(&literal[..split], 0, false);
                assert_eq!(reader.next(), Err(Error::NeedMore));
                assert_eq!(reader.position(), 0);
            }
        }
    }

    #[test]
    fn reports_partial_numbers_as_need_more() {
        let number = b"-12.5e+3,";
        for split in 1..number.len() - 1 {
            let mut reader = Reader::new(&number[..split], 0, false);
            assert_eq!(reader.next(), Err(Error::NeedMore), "split {split}");
            assert_eq!(reader.position(), 0);
        }
        let mut reader = Reader::new(number, 0, false);
        assert_eq!(
            reader.next(),
            Ok(Token::Number {
                value: Number::Float(-12_500.0),
                source: b"-12.5e+3",
            })
        );
    }

    #[test]
    fn rejects_malformed_numbers_and_suffixes() {
        for input in [b"1.]".as_slice(), b"1e]", b"1e+]", b"--1]", b"+1]", b"1x]"] {
            let mut reader = Reader::new(input, 0, true);
            assert_eq!(reader.next(), Err(Error::Invalid), "{input:?}");
            assert_eq!(reader.position(), 0);
        }
    }

    #[test]
    fn saturates_integer_overflow() {
        for (input, expected) in [
            (b"9223372036854775807]".as_slice(), i64::MAX),
            (b"9223372036854775808]", i64::MAX),
            (b"-9223372036854775808]", i64::MIN),
            (b"-9223372036854775809]", i64::MIN),
        ] {
            let mut reader = Reader::new(input, 0, true);
            let Token::Number {
                value: Number::Integer(value),
                ..
            } = reader.next().unwrap()
            else {
                panic!("expected integer for {input:?}")
            };
            assert_eq!(value, expected);
        }
    }

    #[test]
    fn parses_long_float_without_a_fixed_scratch_buffer() {
        let mut input = b"1.".to_vec();
        input.extend(std::iter::repeat_n(b'0', 80));
        input.push(b']');
        let mut reader = Reader::new(&input, 0, true);
        let Token::Number {
            value: Number::Float(value),
            source,
        } = reader.next().unwrap()
        else {
            panic!("expected float")
        };
        assert_eq!(value, 1.0);
        assert_eq!(source.len(), 82);
    }

    #[test]
    fn error_positions_are_stable() {
        let mut invalid = Reader::new(b"  @", 0, true);
        assert_eq!(invalid.next(), Err(Error::Invalid));
        assert_eq!(invalid.position(), 2);

        let mut incomplete = Reader::new(br#""escaped\"#, 0, false);
        assert_eq!(incomplete.next(), Err(Error::NeedMore));
        assert_eq!(incomplete.position(), 0);
    }

    #[test]
    fn eof_after_whitespace_is_invalid() {
        let mut reader = Reader::new(b" \n\r\t", 0, true);
        assert_eq!(reader.next(), Err(Error::Invalid));
        assert!(reader.is_done());
    }
}
