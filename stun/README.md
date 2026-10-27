# STUN CLI

```sh
cd stun

# Anonymous
cargo run -- stun:stun.l.google.com:19302

# Force IPv4 or IPv6
cargo run -- -4 stun:stun.l.google.com:19302
cargo run -- -6 stun:stun.l.google.com:19302

# Detailed output
cargo run -- -4 -v stun:stun.l.google.com:19302

# Authenticated (prompts for password)
cargo run -- stun:turn.example.com:3478 -u my-user

# Explicit password
cargo run -- stun:turn.example.com:3478 -u my-user -w 'my-password'

# ZREST: generate and print one-hour credentials, then test Binding
# Prompts for the shared secret
cargo run -- --zrest stun:turn.example.com:3478 -u my-user-id

# Or pass the shared secret explicitly (also accepts STUN_SHARED_SECRET)
cargo run -- --zrest stun:turn.example.com:3478 -u my-user-id --shared-secret 'my-secret'

# Help
cargo run -- --help
```

Accepted server formats:

```text
stun:stun.l.google.com:19302
stun.l.google.com:19302
stun.example.com
127.0.0.1
[::1]:3478
```

The default port is `3478` when no port is specified.

| Option | Description |
| --- | --- |
| `--zrest` | Generate one-hour ZREST credentials; `-u` is the user ID |
| `--shared-secret SECRET` | Shared secret for ZREST; otherwise prompt without echo |
| `-v`, `--verbose` | Show sent/received STUN messages, attributes, transaction ID, timing and local socket |
| `-4`, `--ipv4` | Use IPv4 only |
| `-6`, `--ipv6` | Use IPv6 only |
| `-u USER` | Enable authentication |
| `-w PASSWORD` | Provide the password; otherwise prompt without echo |
| `-p PORT` | Override the port, including a port specified in the URL |
| `--timeout 5` | Wait up to five seconds per request |

Instead of `-w`, the password can be supplied through `STUN_PASSWORD`.
Without `-4` or `-6`, uses the first address returned by DNS resolution.
