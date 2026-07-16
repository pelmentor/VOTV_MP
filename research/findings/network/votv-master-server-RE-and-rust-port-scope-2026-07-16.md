# Master/lobby + signaling server — RE + Rust-port scope (2026-07-16)

**Type:** point-in-time RE + DESIGN scope → **now AS-BUILT** (the Rust port was written +
wire-verified 2026-07-16; see "## Status" + "## AS-BUILT" below). Not yet deployed to the VPS.
The Rust project lives at `tools/coop-server-rs/` (its README is the operator guide).

**SECURITY:** all live credentials are GITIGNORED and must stay so — never paste them into a committed
file. The VPS SSH creds (IP / user / password) live in `reference_master_server_vps.md` (`.gitignore:114`);
the ops key in `vps_id_ed25519` + pinned host key `vps_known_hosts` (`.gitignore:119`). The master's
runtime secrets are ENV-only (`COOP_TURN_SECRET`, `COOP_MASTER_SIGNING_SECRET`, `COOP_SIGNALING_TOKEN`,
`COOP_SIGNALING_URL`) — never hardcoded. `tools/vps.py` reads the SSH creds from the gitignored md; SSH
prefers key auth (pinned host key), password fallback. Keep this discipline in the Rust port.

## What actually runs on the VPS (it IS in the repo — 2026-07-16 correction)

| File | LOC | Role |
|---|---|---|
| `tools/coop_master_server.py` | 679 | HTTP master/lobby: `/v1/host`, `/v1/heartbeat` (30s), `/v1/leave`, `/v1/visibility`, `/v1/lobbies`, `/v1/join`, `/v1/latest`, `/v1/portcheck`. Pure stdlib (http.server), no framework. |
| `tools/coop_signaling_server.py` | 198 | P2P signaling RELAY — routes opaque SDP/ICE blobs between two peers. NOT a WebRTC/media stack. |
| coturn (not in repo) | — | STUN/TURN on :3478, `static-auth-secret=$COOP_TURN_SECRET`. Mature C daemon — DO NOT rewrite. |
| `tools/vps_provision.sh` | ~250 | provisions the box: generates `TURN_SECRET` (openssl rand) fed to BOTH coturn + the master, writes the systemd units. |
| `tools/vps.py` | ~180 | ops helper (SSH/SFTP via paramiko, key-auth-preferred). |

Client side (the mod, C++): `lobby_announcer.cpp` (host announce + 30s heartbeat + `/v1/leave` on graceful
Stop), `lobby_client.cpp` (browser `/v1/lobbies` fetch), `signaling_client.cpp`, `http_client.h`,
`session_manager.cpp` (drives it). `ui/server_browser.cpp` is the in-mod ImGui browser (NOT the server).

## Master internals worth porting carefully

- **TURN credential minting** (`turn_creds`, master ~L253-267): coturn `use-auth-secret` scheme — username
  `<exp>:<label>`, password `base64(HMAC-SHA1(TURN_SECRET, username))`, `TURN_TTL=120`. Get the HMAC + b64
  byte-exact or TURN auth breaks. The DLL never holds a static TURN password; it fetches a short-lived one.
- **IP spoof-resistance** (`resolve_client_ip`, ~L199): feeds the `/v1/portcheck` UDP probe destination, so
  it must NOT trust `X-Forwarded-For` blindly. Port the trust model faithfully.
- **Lobby lifecycle:** `LOBBY_TTL = 300.0` (master L100) = seconds without a heartbeat before a lobby is
  reaped. `MAX_LOBBIES_GLOBAL = 1000` LRU (evict stalest `last_seen`). `LOBBIES_CACHE_TTL = 5.0` (serve the
  list from a cache — DoS bound). Rate limits: `RL_CREATE`, `RL_JOIN`.
- **Hand-rolled validation:** `clamp_str` + manual dict-key checks everywhere. In Rust this is exactly what
  `serde` typed request structs delete for free — the single biggest maintainability win of the port.

## The ghost-lobby (bug C) — a one-liner, independent of the port

A TASK-KILLED host runs no cleanup, so no `/v1/leave` is sent; the entry lingers up to `LOBBY_TTL`=300s
until the reaper drops it (measured 2026-07-16: browser showed a dead host at 237s/297s age). Fix = lower
`LOBBY_TTL` to ~90 (3 missed 30s heartbeats). Deploying it is a PRODUCTION VPS action (`tools/vps.py put`
+ restart the systemd unit) — do only with the user's explicit go. Do this whether or not the Rust port
happens.

## Rust port scope (grounded — ~877 LOC total)

- **Signaling (198 LOC): trivial.** It's a message router — tokio + a peer-pair map. A weekend.
- **Master (679 LOC): straightforward.** axum or plain hyper + serde (typed request/response structs) + an
  HMAC-SHA1 crate for the TURN creds + a small in-memory `Lobby` map with a reaper task. The careful spots
  are the two above (TURN cred format, IP resolution).
- **Keep coturn as-is.** Only the two Python services move.
- **Migration:** wire-compatible (same JSON endpoints + the same signaling protocol), so run old + new in
  parallel on different ports, cut the client's master URL over, keep `/v1/leave` semantics. No `kProtocol`
  change (that's the P2P wire; this is the master/HTTP wire, versioned separately via `proto` in the body).
- Rust vs Go both fit; Rust's serde + single-static-binary (no Python-on-VPS bitrot) + no-GIL are the
  specific wins for THIS code. Decision: Rust (user, 2026-07-16).

## AS-BUILT (2026-07-16) — the Rust port is written + wire-verified

Built at `tools/coop-server-rs/` (cargo project, two binaries `coop-master` + `coop-signaling`
sharing `src/common.rs`; ~870 LOC Rust). Deps: tokio, serde/serde_json, hmac+sha1, base64,
getrandom, socket2. Faithful to the two careful spots: the TURN cred is **byte-exact**
(`base64(HMAC-SHA1(secret,"<exp>:<label>"))`, unit-tested vs a fixed Python reference vector
`testsecret_abc123` / `1700000000:h0011223344556677` -> `c7pJt+2pR4aVy8LJIi6NtjympwM=`), and the
IP trust model is the rightmost-XFF-from-loopback-proxy rule ported verbatim. serde typed parsing +
tolerant `as_int`/`as_bool`/`coerce_str` reproduce the Python's `int()`/`bool()`/`str()` coercion;
`clamp_str` strips control/separator chars + codepoint-clamps.

**Verification (real, not a smoke-label):** a differential harness spun up all four servers (Rust +
Python, master + signaling) on distinct ports with identical secrets and asserted the Rust responses
are structurally identical to the Python's across every endpoint AND that absolute invariants hold —
**31/31 checks passed**: `/v1/latest` exact + `==py`; host-p2p keyset `==py` with full ICE block;
TURN keyset+ttl=120+2 URIs; lobby-row keyset `==py` with control-char-stripped name, echoed proto,
clamped players_max; `?version=` filter; join-p2p keyset `==py` + `c...` peerIdentity; join-missing
404; heartbeat-bad-token 403; direct-port-out-of-range 400; direct host conn=direct (no ICE); join
direct addr; bad-json 400; 404; **signaling relay delivered `idA deadbeef` A->B on both**; bad-token
greeting dropped on both. Plus 4 `cargo test` unit tests (TURN vector, token_hex shape, clamp, ct_eq).

Behaviour-equivalent improvements (NOT wire changes): multi-thread tokio + one `Mutex<MasterState>`
(no lock across an await); signaling = one-task-owns-the-socket `select!` so evict-on-dup closes the
old socket immediately (dropping the relay-channel Sender is the stop signal); bounded per-dest relay
channel (drop-on-full) replaces the 5s drain. Detail in `tools/coop-server-rs/README.md`.

## Status

- Master/signaling: **AS-IS in Python, still the LIVE service** the coop connects through today.
- Rust port: **AS-BUILT + wire-verified locally (2026-07-16), NOT deployed.** NEXT to ship it:
  cross-compile for the Linux VPS, run it on spare ports alongside the Python, point ONE test client's
  master URL at it, verify a real host+join+relay end-to-end, then cut the production URL over and
  retire the `.py` files (RULE 2 — no dual-run kept).
- Ghost-lobby TTL fix: **identified (master L100), NOT applied** (production VPS action, awaiting go).
  When shipping the Rust master, set its `LOBBY_TTL` (const in `src/bin/master.rs`) to the chosen value
  (~90s) so the cutover carries the fix rather than re-porting 300.
