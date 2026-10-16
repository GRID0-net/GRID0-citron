# Playing online through SwitchNet

[SwitchNet](https://github.com/n-popescu/switchnet) is a self-hosted replacement for the Nintendo
servers a Switch talks to. This build of citron can point a game at one instead of Nintendo.

Everything here is off until you fill in the settings, and it only ever sends a game's traffic to
the server you name.

## Setting it up

In *Emulation → Configure → Network*, under **Private Nintendo Servers (SwitchNet)**:

| Setting | What to put there |
| --- | --- |
| **Redirect address** | The IP address of your SwitchNet server. Every Nintendo hostname the game looks up resolves there. |
| **NAT-check secondary address** | A second address for `nncs2`, if a game you play uses Pia. Splatoon 3 does not. |
| **Trusted CA certificate** | Leave empty to use the built-in SwitchNet Local CA. Point it at a PEM file only if your server's certificate is signed by a different CA. Takes effect after a restart. |
| **SwitchNet server** | Where the server is, as `host` or `host:port` (for example `192.168.1.50`, or `192.168.1.50:8443` if the edge is not on 443). |
| **Username** / **Password** | The ones you chose on the server's `/register` page. |

Point the redirect address and the SwitchNet server at the same machine. They are separate
mechanisms and nothing forces them to agree; if they disagree, the game talks to one server while
the emulator logs in to another, and the game server rejects a token it never issued.

**Airplane Mode** must be off.

## What each part does

* **Redirect.** Lookups of Nintendo-owned domains (`nintendo.net`, `nintendo.com` and the other
  Nintendo domains SwitchNet's own DNS configuration lists) are answered with the redirect address.
  Nothing else is redirected. Hosts your server does not serve then fail against your server
  instead of reaching Nintendo.
* **Certificate trust.** The emulated `ssl` service trusts the SwitchNet CA in addition to the
  usual roots. It does not turn verification off: a certificate for the wrong hostname is still
  refused.
* **Login.** The emulator logs in with `POST /login` using your username and password, and hands
  the game the identity token your server signed, in place of the token it would otherwise make up.
  The server's certificate is checked against the same CA before your password is sent.
* **Splatoon 3.** The game does TLS itself with pinned certificates, so trusting a CA never reaches
  it. No patch ships inside citron: at every start it compares `<citron dir>/grid0plus/patches/`
  with the newest `patches-<serial>` release of GRID0plus-cfw (the same release the console
  Toolbox installs from), and if anything differs asks to download it and restart. Each file is
  checked against the release manifest's SHA-256 before it is written. The patches are applied in
  memory whenever a redirect address is set. They match the game's exact build: a title update can
  stop them applying.
* **Sockets.** The game's online client (gRPC) wakes its network loop through an `eventfd`, which
  citron did not implement. It now does, and a `poll` that includes one no longer ties up a socket
  service thread while it waits.

## When it does not work

Look for these in the log:

| Log line | What to do |
| --- | --- |
| `SwitchNet login failed: login was refused (401)` | the username or password is wrong |
| `SwitchNet login failed: could not reach …` | the server address is wrong, the server is not running, or its certificate is not signed by the trusted CA |
| `SwitchNet login failed: '…' is not a host or host:port` | the SwitchNet server field is malformed |
| `SwitchNet trace: … answered with …` never appears | the redirect address is empty, or the game resolved nothing yet |
| `SwitchNet trace:` lines stop with no `connect` | the game resolved the server but never opened a connection to it: send the log |
| `SwitchNet trace: connect … -> errno=115`, and `Connect fd=… failed with errno=115` | expected: the game connects without waiting (EINPROGRESS) and learns the result from a later `poll` |
| `SwitchNet trace: connect … -> errno=111` (or `110`) | the server refused the connection, or it timed out: check that this machine can reach the server on 443 |

Every line that follows a lookup of a Nintendo host starts with `SwitchNet trace:` and is logged at
Warning, so it is in `citron_log.txt` with the default log filter: the socket calls the game made
next, up to 256 of them per lookup. When reporting a problem, stop emulation before copying the
log, so that it is complete.

### Splatoon 3 three-player startup readiness

The bundled 11.3.0 small-match IPS matches GRID0+ Toolbox 0.8.10. It waits for
a completed assigned `All` peer-group roster and matching connected Pia count,
rather than bypassing missing/unready connectivity. Native reference ownership
is retained while inspecting the group; the original stability counter remains.
A Switch-hosted three-player hotspot battle succeeded with this patch. Other
build IDs remain unchanged. Restart the game to load the updated embedded IPS.
