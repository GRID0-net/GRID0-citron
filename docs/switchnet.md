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
  it. The two patches a console running SwitchNet installs as `exefs_patches` are applied
  automatically, in memory, whenever a redirect address is set. They match the game's exact build:
  a title update can stop them applying.
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
| `Redirecting … to private server …` never appears | the redirect address is empty, or the game resolved nothing yet |
