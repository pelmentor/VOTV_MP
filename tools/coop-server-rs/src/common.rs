//! Shared helpers for the coop master + signaling binaries.
//!
//! RULE 3: this is VPS infra, it never ships in the mod. Ported 1:1 from the
//! Python originals (`tools/coop_master_server.py`, `tools/coop_signaling_server.py`)
//! and kept WIRE-COMPATIBLE with them so the old + new services can run in
//! parallel during cutover. The byte-exact spots (the TURN credential HMAC and the
//! identity-string shapes) are called out inline — a mismatch there breaks coturn
//! auth or the signaling rendezvous silently.

use base64::engine::general_purpose::{STANDARD as B64, URL_SAFE_NO_PAD as B64URL};
use base64::Engine;
use hmac::{Hmac, Mac};
use sha1::Sha1;
use std::time::{SystemTime, UNIX_EPOCH};

type HmacSha1 = Hmac<Sha1>;

/// stdout line log with an explicit flush (systemd journal picks it up). Mirrors
/// the Python `log()`.
pub fn log(msg: &str) {
    use std::io::Write;
    let mut out = std::io::stdout().lock();
    let _ = writeln!(out, "{msg}");
    let _ = out.flush();
}

/// Read an env var as a String, falling back to `default` when unset/empty-allowed.
pub fn env_str(key: &str, default: &str) -> String {
    std::env::var(key).unwrap_or_else(|_| default.to_string())
}

/// Read an env var as an integer, falling back to `default` on unset/parse-fail.
pub fn env_int(key: &str, default: i64) -> i64 {
    std::env::var(key)
        .ok()
        .and_then(|v| v.trim().parse::<i64>().ok())
        .unwrap_or(default)
}

/// Seconds since the Unix epoch (wall clock). Used ONLY for the TURN credential
/// expiry, which coturn validates against real time — never for rate/last-seen
/// windows (those use a monotonic `Instant`).
pub fn now_unix() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// `secrets.token_hex(nbytes)` — `2*nbytes` lowercase hex chars from the OS CSPRNG.
pub fn token_hex(nbytes: usize) -> String {
    let mut buf = vec![0u8; nbytes];
    getrandom::getrandom(&mut buf).expect("OS CSPRNG unavailable");
    let mut s = String::with_capacity(nbytes * 2);
    for b in &buf {
        s.push(char::from_digit((b >> 4) as u32, 16).unwrap());
        s.push(char::from_digit((b & 0xf) as u32, 16).unwrap());
    }
    s
}

/// `secrets.token_urlsafe(nbytes)` — base64url (no padding) of `nbytes` CSPRNG bytes
/// (~1.3*nbytes chars). Used for the host bearer `token` capability.
pub fn token_urlsafe(nbytes: usize) -> String {
    let mut buf = vec![0u8; nbytes];
    getrandom::getrandom(&mut buf).expect("OS CSPRNG unavailable");
    B64URL.encode(&buf)
}

/// `clamp_str(v, maxlen)` — strip control/whitespace-separator chars (keep printable
/// + ASCII space), clamp to `maxlen` code points. Mirrors the Python `clamp_str`
/// (which uses `str.isprintable()`); a host-supplied `name`/`world`/`version` is a
/// cheap DoS on every client that fetches the list, hence the strip + clamp.
pub fn clamp_str(s: &str, maxlen: usize) -> String {
    s.chars()
        .filter(|c| *c == ' ' || (!c.is_control() && !c.is_whitespace()))
        .take(maxlen)
        .collect()
}

/// Constant-time byte compare, matching Python's `hmac.compare_digest` (which is
/// also length-leaking — an early length mismatch returns fast). Used for the
/// server-side capability tokens (host bearer, signaling shared bearer).
pub fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

/// A coturn REST time-limited credential (design 7), byte-for-byte identical to the
/// Python `turn_creds()`:
///   username = "<unixExpiry>:<label>"
///   password = base64( HMAC-SHA1( TURN_SECRET, username ) )
/// coturn validates it via `use-auth-secret` / `static-auth-secret=TURN_SECRET`.
/// **Byte-exact spot:** the HMAC digest, the base64 alphabet (STANDARD, with `=`
/// padding), and the `"exp:label"` username format must all match or coturn auth
/// fails. Returns `None` (→ omitted from the JSON, same as the Python empty dict)
/// when TURN is not configured.
/// The base64(HMAC-SHA1(secret, username)) coturn password. Split out so the
/// byte-exact spot is unit-testable against the Python reference independent of the
/// time-based expiry.
pub fn turn_password(turn_secret: &str, username: &str) -> String {
    let mut mac = HmacSha1::new_from_slice(turn_secret.as_bytes()).expect("HMAC accepts any key length");
    mac.update(username.as_bytes());
    B64.encode(mac.finalize().into_bytes())
}

pub fn turn_creds(turn_uri: &str, turn_secret: &str, label: &str, ttl: u64) -> Option<serde_json::Value> {
    if turn_uri.is_empty() || turn_secret.is_empty() {
        return None;
    }
    let exp = now_unix() + ttl;
    let username = format!("{exp}:{label}");
    let password = turn_password(turn_secret, &username);
    let uris = vec![
        format!("{turn_uri}?transport=udp"),
        format!("{turn_uri}?transport=tcp"),
    ];
    Some(serde_json::json!({
        "user": username,
        "pass": password,
        "ttl": ttl,
        "uris": uris,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turn_password_matches_python_reference() {
        // Reference produced by the Python turn_creds() HMAC path:
        //   python -c "import hmac,hashlib,base64; u='1700000000:h0011223344556677';
        //   print(base64.b64encode(hmac.new(b'testsecret_abc123', u.encode(),
        //   hashlib.sha1).digest()).decode())"  -> c7pJt+2pR4aVy8LJIi6NtjympwM=
        let pw = turn_password("testsecret_abc123", "1700000000:h0011223344556677");
        assert_eq!(pw, "c7pJt+2pR4aVy8LJIi6NtjympwM=");
    }

    #[test]
    fn token_hex_shape() {
        // secrets.token_hex(8) -> 16 lowercase hex chars.
        let t = token_hex(8);
        assert_eq!(t.len(), 16);
        assert!(t.chars().all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase()));
    }

    #[test]
    fn clamp_str_strips_control_and_clamps() {
        assert_eq!(clamp_str("hello\tworld\n", 64), "helloworld");
        assert_eq!(clamp_str("keep spaces", 64), "keep spaces");
        assert_eq!(clamp_str("abcdef", 3), "abc");
    }

    #[test]
    fn ct_eq_basic() {
        assert!(ct_eq(b"abc", b"abc"));
        assert!(!ct_eq(b"abc", b"abd"));
        assert!(!ct_eq(b"abc", b"ab"));
    }
}
