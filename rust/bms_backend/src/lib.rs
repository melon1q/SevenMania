#![deny(unsafe_op_in_unsafe_fn)]

use std::collections::BTreeMap;
use std::ffi::{OsString, c_void};
use std::fs;
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

const MAX_INPUT: usize = 16 * 1024 * 1024; // UTF-16 units; larger inputs use C++.
const MAX_OUTPUT: usize = 128 * 1024 * 1024;
const MAX_NODES: usize = 1_000_000;
static COUNTS: [AtomicU64; 4] = [const { AtomicU64::new(0) }; 4];

#[derive(Debug)]
struct Failure {
    status: i32,
    offset: usize,
}
type Result<T> = std::result::Result<T, Failure>;
fn invalid(offset: usize) -> Failure {
    Failure { status: 1, offset }
}
fn limited() -> Failure {
    Failure {
        status: 5,
        offset: 0,
    }
}

#[repr(C)]
pub struct Buffer {
    data: *mut u8,
    len: usize,
    detail: usize,
    status: i32,
}
#[derive(Clone, Copy)]
#[repr(C)]
pub struct Characters {
    flags: Option<extern "C" fn(u16) -> u8>,
    lower: Option<extern "C" fn(u16) -> u16>,
}
type Cancel = Option<extern "C" fn(*const c_void) -> i32>;

struct Wire(Vec<u8>);
impl Wire {
    fn bytes(&mut self, bytes: &[u8]) -> Result<()> {
        if self.0.len().checked_add(bytes.len()).ok_or_else(limited)? > MAX_OUTPUT {
            return Err(limited());
        }
        self.0.extend_from_slice(bytes);
        Ok(())
    }
    fn count(&mut self, n: usize) -> Result<()> {
        self.bytes(&u32::try_from(n).map_err(|_| limited())?.to_le_bytes())
    }
    fn text(&mut self, s: &[u16]) -> Result<()> {
        self.count(s.len())?;
        for c in s {
            self.bytes(&c.to_le_bytes())?;
        }
        Ok(())
    }
}

fn lines(text: &[u16]) -> Result<Vec<u8>> {
    // MSVC wchar_t char_traits uses 0xffff as WEOF in wistringstream/getline.
    let text = &text[..text.iter().position(|&c| c == 0xffff).unwrap_or(text.len())];
    let mut out = Wire(Vec::with_capacity(
        text.len().saturating_mul(2).min(MAX_OUTPUT),
    ));
    let count = text.iter().filter(|&&c| c == 10).count()
        + usize::from(text.last().is_some_and(|&c| c != 10));
    out.count(count)?;
    let mut begin = 0;
    for end in 0..=text.len() {
        if end == text.len() || text[end] == 10 {
            if end == text.len() && begin == end {
                break;
            }
            let content_end = if end > begin && text[end - 1] == 13 {
                end - 1
            } else {
                end
            };
            out.text(&text[begin..content_end])?;
            begin = end + 1;
        }
    }
    Ok(out.0)
}

enum Value {
    Null,
    Number(f64),
    Text(Vec<u16>),
    Bool(bool),
    Array(Vec<Value>),
    Object(BTreeMap<Vec<u16>, Value>),
}
struct Json<'a> {
    text: &'a [u16],
    at: usize,
    nodes: usize,
}
impl Json<'_> {
    fn space(&mut self) {
        while self
            .text
            .get(self.at)
            .is_some_and(|c| matches!(c, 32 | 9 | 10 | 13))
        {
            self.at += 1;
        }
    }
    fn take(&mut self, c: u16) -> bool {
        self.space();
        if self.text.get(self.at) == Some(&c) {
            self.at += 1;
            true
        } else {
            false
        }
    }
    fn string(&mut self) -> Result<Vec<u16>> {
        if !self.take(34) {
            return Err(invalid(self.at));
        }
        let mut s = Vec::new();
        while let Some(&c) = self.text.get(self.at) {
            self.at += 1;
            if c == 34 {
                return Ok(s);
            }
            if c < 32 {
                return Err(invalid(self.at));
            }
            if c != 92 {
                s.push(c);
                continue;
            }
            let c = *self.text.get(self.at).ok_or_else(|| invalid(self.at))?;
            self.at += 1;
            s.push(match c {
                34 | 47 | 92 => c,
                98 => 8,
                102 => 12,
                110 => 10,
                114 => 13,
                116 => 9,
                117 => {
                    let mut code = 0;
                    for _ in 0..4 {
                        let c = *self.text.get(self.at).ok_or_else(|| invalid(self.at))?;
                        self.at += 1;
                        let digit = match c {
                            48..=57 => c - 48,
                            65..=70 => c - 65 + 10,
                            97..=102 => c - 97 + 10,
                            _ => return Err(invalid(self.at)),
                        };
                        code = code * 16 + digit;
                    }
                    code // Preserve UTF-16 surrogates, as the existing Windows reader does.
                }
                _ => return Err(invalid(self.at)),
            });
        }
        Err(invalid(self.at))
    }
    fn literal(&mut self, word: &[u16]) -> bool {
        if self.text.get(self.at..self.at + word.len()) == Some(word) {
            self.at += word.len();
            true
        } else {
            false
        }
    }
    fn value(&mut self, depth: usize) -> Result<Value> {
        if depth > 128 {
            return Err(invalid(self.at));
        }
        self.nodes += 1;
        if self.nodes > MAX_NODES {
            return Err(limited());
        }
        self.space();
        let c = *self.text.get(self.at).ok_or_else(|| invalid(self.at))?;
        if c == 34 {
            return Ok(Value::Text(self.string()?));
        }
        if self.take(123) {
            let mut values = BTreeMap::new();
            if self.take(125) {
                return Ok(Value::Object(values));
            }
            loop {
                let key = self.string()?;
                if !self.take(58) {
                    return Err(invalid(self.at));
                }
                values.insert(key, self.value(depth + 1)?);
                if !self.take(44) {
                    break;
                }
            }
            if !self.take(125) {
                return Err(invalid(self.at));
            }
            return Ok(Value::Object(values));
        }
        if self.take(91) {
            let mut values = Vec::new();
            if self.take(93) {
                return Ok(Value::Array(values));
            }
            loop {
                values.push(self.value(depth + 1)?);
                if !self.take(44) {
                    break;
                }
            }
            if !self.take(93) {
                return Err(invalid(self.at));
            }
            return Ok(Value::Array(values));
        }
        if self.literal(&[110, 117, 108, 108]) {
            return Ok(Value::Null);
        }
        if self.literal(&[116, 114, 117, 101]) {
            return Ok(Value::Bool(true));
        }
        if self.literal(&[102, 97, 108, 115, 101]) {
            return Ok(Value::Bool(false));
        }
        let start = self.at;
        if self.text.get(self.at) == Some(&45) {
            self.at += 1;
        }
        match self.text.get(self.at) {
            Some(48) => self.at += 1,
            Some(49..=57) => {
                while self
                    .text
                    .get(self.at)
                    .is_some_and(|c| (48..=57).contains(c))
                {
                    self.at += 1;
                }
            }
            _ => return Err(invalid(self.at)),
        }
        if self.text.get(self.at) == Some(&46) {
            self.at += 1;
            let begin = self.at;
            while self
                .text
                .get(self.at)
                .is_some_and(|c| (48..=57).contains(c))
            {
                self.at += 1;
            }
            if begin == self.at {
                return Err(invalid(self.at));
            }
        }
        if self
            .text
            .get(self.at)
            .is_some_and(|c| matches!(c, 69 | 101))
        {
            self.at += 1;
            if self.text.get(self.at).is_some_and(|c| matches!(c, 43 | 45)) {
                self.at += 1;
            }
            let begin = self.at;
            while self
                .text
                .get(self.at)
                .is_some_and(|c| (48..=57).contains(c))
            {
                self.at += 1;
            }
            if begin == self.at {
                return Err(invalid(self.at));
            }
        }
        let ascii: Vec<u8> = self.text[start..self.at].iter().map(|c| *c as u8).collect();
        let number = std::str::from_utf8(&ascii)
            .map_err(|_| invalid(start))?
            .parse::<f64>()
            .map_err(|_| invalid(start))?;
        let nonzero = ascii
            .iter()
            .take_while(|&&c| c != b'e' && c != b'E')
            .any(|c| (b'1'..=b'9').contains(c));
        if !number.is_finite() || (nonzero && number.abs() < f64::MIN_POSITIVE) {
            return Err(invalid(start));
        }
        Ok(Value::Number(number))
    }
}
fn encode(value: &Value, out: &mut Wire) -> Result<()> {
    match value {
        Value::Null => out.bytes(&[0]),
        Value::Number(n) => {
            out.bytes(&[1])?;
            out.bytes(&n.to_le_bytes())
        }
        Value::Text(s) => {
            out.bytes(&[2])?;
            out.text(s)
        }
        Value::Bool(b) => out.bytes(&[3, u8::from(*b)]),
        Value::Array(a) => {
            out.bytes(&[4])?;
            out.count(a.len())?;
            for v in a {
                encode(v, out)?;
            }
            Ok(())
        }
        Value::Object(a) => {
            out.bytes(&[5])?;
            out.count(a.len())?;
            for (k, v) in a {
                out.text(k)?;
                encode(v, out)?;
            }
            Ok(())
        }
    }
}
fn json(text: &[u16]) -> Result<Vec<u8>> {
    let mut reader = Json {
        text,
        at: 0,
        nodes: 0,
    };
    let value = reader.value(0)?;
    reader.space();
    if reader.at != text.len() {
        return Err(invalid(reader.at));
    }
    let mut out = Wire(Vec::new());
    encode(&value, &mut out)?;
    Ok(out.0)
}

fn scan(root: &[u16], cancel: Cancel, context: *const c_void) -> Result<Vec<u8>> {
    let mut stack = Vec::new();
    let root = PathBuf::from(OsString::from_wide(root));
    if let Ok(dir) = fs::read_dir(root) {
        stack.push(dir);
    }
    let mut paths = Vec::new();
    while let Some(dir) = stack.last_mut() {
        if cancel.is_some_and(|f| f(context) != 0) {
            return Err(Failure {
                status: 3,
                offset: 0,
            });
        }
        let Some(next) = dir.next() else {
            stack.pop();
            continue;
        };
        let Ok(entry) = next else {
            continue;
        };
        let Ok(kind) = entry.file_type() else {
            continue;
        };
        // Match the native scanner: never follow Windows junctions/reparse
        // directories or scan our disposable metadata store.
        use std::os::windows::fs::MetadataExt;
        if kind.is_dir() && !kind.is_symlink() {
            if entry.file_name() == ".sevenmania-index"
                || entry.metadata().is_ok_and(|m| m.file_attributes() & 0x400 != 0)
            {
                continue;
            }
            if let Ok(child) = fs::read_dir(entry.path()) {
                stack.push(child);
            }
            continue;
        }
        let path = entry.path();
        let chart = path.extension().and_then(|s| s.to_str()).is_some_and(|s| {
            ["bms", "bme", "bml", "pms", "bmson", "mbm"]
                .iter()
                .any(|e| s.eq_ignore_ascii_case(e))
        });
        if chart && !kind.is_symlink()
            && entry.metadata().is_ok_and(|m| m.file_attributes() & 0x400 == 0)
            && path.is_file() {
            paths.push(path.as_os_str().encode_wide().collect::<Vec<_>>());
        }
        if paths.len() > MAX_NODES {
            return Err(limited());
        }
    }
    let mut out = Wire(Vec::new());
    out.count(paths.len())?;
    for p in paths {
        out.text(&p)?;
    }
    Ok(out.0)
}

impl Characters {
    fn flags(self, c: u16) -> u8 {
        if c < 128 {
            u8::from(matches!(c, 9..=13 | 32)) | (u8::from((48..=57).contains(&c)) << 1)
        } else {
            self.flags.map_or(0, |f| f(c))
        }
    }
    fn space(self, c: u16) -> bool {
        self.flags(c) & 1 != 0
    }
    fn digit(self, c: u16) -> bool {
        self.flags(c) & 2 != 0
    }
    fn lowercase(self, s: &[u16]) -> Vec<u16> {
        s.iter()
            .map(|&c| {
                if (65..=90).contains(&c) {
                    c + 32
                } else if c < 128 {
                    c
                } else {
                    self.lower.map_or(c, |f| f(c))
                }
            })
            .collect()
    }
    fn trim(self, s: &[u16]) -> Vec<u16> {
        let mut end = s.len();
        while end > 0 && self.space(s[end - 1]) {
            end -= 1;
        }
        let mut begin = 0;
        while begin < end && matches!(s[begin], 32 | 9 | 10 | 13) {
            begin += 1;
        }
        s[begin..end].to_vec()
    }
}
fn ascii(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}
fn round_label(s: &[u16], ops: Characters) -> bool {
    let mut label = ops.lowercase(&ops.trim(s));
    if label.starts_with(&ascii("sp ")) || label.starts_with(&ascii("dp ")) {
        label = ops.trim(&label[3..]);
    }
    [
        "beginner", "basic", "easy", "light", "normal", "hyper", "another", "insane",
    ]
    .iter()
    .any(|s| {
        let word = ascii(s);
        label == word
            || (label.starts_with(&word)
                && label
                    .get(word.len())
                    .is_some_and(|&c| ops.space(c) || ops.digit(c)))
    })
}
fn suffix(s: &[u16], ops: Characters) -> bool {
    let mut compact: Vec<u16> = ops
        .lowercase(&ops.trim(s))
        .into_iter()
        .filter(|&c| !ops.space(c) && !matches!(c, 45 | 95 | 58 | 126))
        .collect();
    if compact.starts_with(&ascii("sp")) || compact.starts_with(&ascii("dp")) {
        compact.drain(..2);
    }
    for word in [
        "beginner",
        "basic",
        "easy",
        "light",
        "normal",
        "hyper",
        "another",
        "insane",
        "leggendaria",
        "extra",
        "expert",
        "starter",
    ] {
        let word = ascii(word);
        if !compact.starts_with(&word) {
            continue;
        }
        let mut tail = &compact[word.len()..];
        if tail.starts_with(&ascii("lv")) {
            tail = &tail[2..];
        }
        while !tail.is_empty() && ops.digit(tail[0]) {
            tail = &tail[1..];
        }
        return tail.is_empty() || tail == ascii("key") || tail == ascii("keys");
    }
    false
}
fn title(input: &[u16], ops: Characters) -> Vec<u16> {
    let mut current = input.to_vec();
    loop {
        // Shrinking iteration replaces unbounded recursive suffix stripping.
        let mut base = ops.trim(&current);
        let mut stripped = false;
        for c in &mut base {
            *c = match *c {
                0xff01..=0xff5e => *c - 0xfee0,
                0x3000 => 32,
                0x2013 | 0x2014 | 0x2212 => 45,
                0x3010 => 91,
                0x3011 => 93,
                _ => *c,
            };
        }
        while base.last().is_some_and(|c| matches!(c, 93 | 41)) {
            let closing = *base.last().unwrap();
            let opener = if closing == 41 { 40 } else { 91 };
            let mut depth = 0;
            let mut opening = None;
            for i in (0..base.len()).rev() {
                if base[i] == closing {
                    depth += 1;
                } else if base[i] == opener {
                    depth -= 1;
                    if depth == 0 {
                        opening = Some(i);
                        break;
                    }
                }
            }
            let Some(opening) = opening else {
                break;
            };
            if opening == 0 {
                break;
            }
            if closing == 41 && !round_label(&base[opening + 1..base.len() - 1], ops) {
                break;
            }
            let candidate = ops.trim(&base[..opening]);
            if candidate.is_empty() {
                break;
            }
            base = candidate;
            stripped = true;
        }
        let mut next = None;
        for i in 1..base.len() {
            if !ops.space(base[i]) && !matches!(base[i], 45 | 95 | 58 | 126) {
                continue;
            }
            if suffix(&base[i..], ops) {
                let candidate = ops.trim(&base[..i]);
                if !candidate.is_empty() {
                    next = Some(candidate);
                    break;
                }
            }
        }
        if let Some(next) = next {
            current = next;
            continue;
        }
        return if stripped { base } else { ops.trim(&current) };
    }
}

fn guard(f: impl FnOnce() -> Result<Vec<u8>>) -> Buffer {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(bytes)) => {
            let mut bytes = bytes.into_boxed_slice();
            let data = bytes.as_mut_ptr();
            let len = bytes.len();
            std::mem::forget(bytes);
            Buffer {
                data,
                len,
                detail: 0,
                status: 0,
            }
        }
        Ok(Err(e)) => Buffer {
            data: std::ptr::null_mut(),
            len: 0,
            detail: e.offset,
            status: e.status,
        },
        Err(_) => Buffer {
            data: std::ptr::null_mut(),
            len: 0,
            detail: 0,
            status: 4,
        },
    }
}
/// # Safety
/// Input must be a readable UTF-16 span for this call. Callbacks must not throw or unwind.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_rust_run(
    op: u32,
    input: *const u16,
    len: usize,
    ops: Characters,
    cancel: Cancel,
    context: *const c_void,
) -> Buffer {
    guard(|| {
        if len > MAX_INPUT {
            return Err(limited());
        }
        if len != 0 && (input.is_null() || input as usize % 2 != 0) {
            return Err(invalid(0));
        }
        // SAFETY: non-empty pointer validity/lifetime is guaranteed by the C++ span wrapper.
        let text = if len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(input, len) }
        };
        let bytes = match op {
            1 => lines(text),
            2 => json(text),
            3 => scan(text, cancel, context),
            4 => {
                if ops.flags.is_none() || ops.lower.is_none() {
                    return Err(invalid(0));
                }
                let mut out = Wire(Vec::new());
                out.text(&title(text, ops))?;
                Ok(out.0)
            }
            _ => Err(invalid(0)),
        }?;
        COUNTS[op as usize - 1].fetch_add(1, Ordering::Relaxed);
        Ok(bytes)
    })
}
/// # Safety
/// Pass exactly one live allocation returned by bms_rust_run, exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn bms_rust_free(data: *mut u8, len: usize) {
    if !data.is_null() {
        // SAFETY: same boxed-slice allocation, length and allocator as guard().
        drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(data, len)) });
    }
}
#[unsafe(no_mangle)]
pub extern "C" fn bms_rust_abi_version() -> u32 {
    1
}
#[unsafe(no_mangle)]
pub extern "C" fn bms_rust_successes(op: u32) -> u64 {
    COUNTS
        .get(op.wrapping_sub(1) as usize)
        .map_or(0, |n| n.load(Ordering::Relaxed))
}

#[cfg(test)]
mod tests {
    use super::*;
    extern "C" fn flags(c: u16) -> u8 {
        u8::from(char::from_u32(c as u32).is_some_and(char::is_whitespace))
    }
    extern "C" fn lower(c: u16) -> u16 {
        c
    }
    #[test]
    fn json_edges() {
        for s in [
            "null",
            "[true,false,0,-0,1e3]",
            r#"{"a":1,"a":2,"u":"\ud800\u0000"}"#,
        ] {
            assert!(json(&ascii(s)).is_ok(), "{s}");
        }
        for s in [
            "",
            "[1,]",
            "{\"a\":}",
            "01",
            "1.",
            "1e",
            "1e999",
            "1e-999",
            "true false",
            "\"\n\"",
        ] {
            assert!(json(&ascii(s)).is_err(), "{s}");
        }
        assert!(json(&ascii(&format!("{}0{}", "[".repeat(130), "]".repeat(130)))).is_err());
    }
    #[test]
    fn line_boundaries() {
        for (s, n) in [("", 0), ("\n", 1), ("a\r\nb\n", 2), ("a\n\n", 2)] {
            let out = lines(&ascii(s)).unwrap();
            assert_eq!(u32::from_le_bytes(out[..4].try_into().unwrap()), n);
        }
    }
    #[test]
    fn titles() {
        let ops = Characters {
            flags: Some(flags),
            lower: Some(lower),
        };
        for (s, want) in [
            ("Signager (Another)", "Signager"),
            ("-零-【NORMAL】", "-零-"),
            ("Song (Remix)", "Song (Remix)"),
            ("Song -SP STARTER-", "Song"),
        ] {
            assert_eq!(title(&ascii(s), ops), ascii(want));
        }
    }
    #[test]
    fn panic_boundary() {
        assert_eq!(guard(|| panic!("test panic")).status, 4);
    }
    #[test]
    fn null_boundary() {
        let result = unsafe {
            bms_rust_run(
                1,
                std::ptr::null(),
                1,
                Characters {
                    flags: None,
                    lower: None,
                },
                None,
                std::ptr::null(),
            )
        };
        assert_eq!(result.status, 1);
    }
    #[test]
    fn bounded_fuzz() {
        let mut state = 7u64;
        for _ in 0..5000 {
            let mut input = Vec::new();
            for _ in 0..(state as usize % 128) {
                state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
                input.push((state >> 32) as u16);
            }
            let _ = json(&input);
            let _ = lines(&input);
        }
    }
}
