use base64::{engine::general_purpose::STANDARD, Engine};
use clap::Parser;
use ring::hmac;
use std::{
    error::Error,
    io,
    net::{SocketAddr, ToSocketAddrs, UdpSocket},
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use stun::{
    attributes::*, error_code::ErrorCodeAttribute, fingerprint::FINGERPRINT,
    integrity::MessageIntegrity, message::*, textattrs::TextAttribute, xoraddr::XorMappedAddress,
};

type Result<T> = std::result::Result<T, Box<dyn Error>>;

#[derive(Parser)]
#[command(about = "UDP STUN Binding request, optionally authenticated for coturn secure-stun")]
struct Args {
    /// Generate one-hour ZREST credentials; -u becomes the user ID
    #[arg(long = "zrest", requires = "username", conflicts_with = "password")]
    z_rest: bool,
    /// ZREST shared secret; otherwise prompt without echo
    #[arg(
        long,
        env = "STUN_SHARED_SECRET",
        hide_env_values = true,
        requires = "z_rest"
    )]
    shared_secret: Option<String>,
    /// Show messages, attributes, local socket and transaction timing
    #[arg(short, long)]
    verbose: bool,
    /// Use IPv4 only
    #[arg(short = '4', long, conflicts_with = "ipv6")]
    ipv4: bool,
    /// Use IPv6 only
    #[arg(short = '6', long, conflicts_with = "ipv4")]
    ipv6: bool,
    /// STUN URL, hostname, IP, or host:port
    host: String,
    /// Override URL port (default: 3478)
    #[arg(short, long)]
    port: Option<u16>,
    /// STUN username, or user ID with --zrest
    #[arg(short, long)]
    username: Option<String>,
    /// Otherwise read STUN_PASSWORD or prompt without echo
    #[arg(
        short = 'w',
        long,
        env = "STUN_PASSWORD",
        hide_env_values = true,
        requires = "username"
    )]
    password: Option<String>,
    /// Timeout per transaction, in seconds
    #[arg(long, default_value_t = 5, value_parser = clap::value_parser!(u64).range(1..=60))]
    timeout: u64,
}

fn request(auth: Option<(&str, &str, &str, &MessageIntegrity)>) -> Result<Message> {
    let mut m = Message::new();
    m.typ = BINDING_REQUEST;
    m.new_transaction_id()?;
    m.write_header();
    if let Some((username, realm, nonce, integrity)) = auth {
        TextAttribute::new(ATTR_USERNAME, username.into()).add_to(&mut m)?;
        TextAttribute::new(ATTR_REALM, realm.into()).add_to(&mut m)?;
        TextAttribute::new(ATTR_NONCE, nonce.into()).add_to(&mut m)?;
        integrity.add_to(&mut m)?;
    }
    FINGERPRINT.add_to(&mut m)?;
    Ok(m)
}

fn exchange(
    socket: &UdpSocket,
    request: &Message,
    timeout: Duration,
    verbose: bool,
) -> Result<Message> {
    let started = Instant::now();
    let deadline = started + timeout;
    let mut interval = Duration::from_millis(500);
    let mut buffer = [0; 65535];
    loop {
        socket.send(&request.raw)?;
        if verbose {
            dump_message("Sent", request);
        }
        let retry_at = (Instant::now() + interval).min(deadline);
        while Instant::now() < retry_at {
            socket.set_read_timeout(Some(
                retry_at
                    .saturating_duration_since(Instant::now())
                    .max(Duration::from_millis(1)),
            ))?;
            match socket.recv(&mut buffer) {
                Ok(n) => {
                    let mut response = Message::new();
                    response.unmarshal_binary(&buffer[..n])?;
                    if response.transaction_id != request.transaction_id {
                        if verbose {
                            println!("Ignored response with a different transaction ID");
                        }
                        continue;
                    }
                    if response.contains(ATTR_FINGERPRINT) {
                        FINGERPRINT.check(&response)?;
                        if verbose {
                            println!("FINGERPRINT verified");
                        }
                    }
                    if verbose {
                        println!(
                            "Transaction elapsed: {:.2} ms (including any retries)",
                            started.elapsed().as_secs_f64() * 1000.0
                        );
                        dump_message("Received", &response);
                    }
                    return Ok(response);
                }
                Err(e)
                    if matches!(
                        e.kind(),
                        io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut
                    ) =>
                {
                    break
                }
                Err(e) => return Err(e.into()),
            }
        }
        if Instant::now() >= deadline {
            return Err("STUN response timed out".into());
        }
        interval = (interval * 2).min(Duration::from_secs(2));
    }
}

fn dump_message(direction: &str, message: &Message) {
    println!("{direction}: {} ({} bytes)", message.typ, message.raw.len());
    println!("  Transaction ID: {:02x?}", message.transaction_id);
    for attribute in &message.attributes.0 {
        let value = if matches!(
            attribute.typ,
            ATTR_SOFTWARE | ATTR_USERNAME | ATTR_REALM | ATTR_NONCE
        ) {
            format!("{:?}", String::from_utf8_lossy(&attribute.value))
        } else {
            format!("{:02x?}", attribute.value)
        };
        println!("  {} ({} bytes): {value}", attribute.typ, attribute.length);
    }
}

fn error_code(m: &Message) -> Result<u16> {
    if m.typ != BINDING_ERROR {
        return Err(format!("Expected Binding Error, received {}", m.typ).into());
    }
    let mut error = ErrorCodeAttribute::default();
    error.get_from(m)?;
    Ok(error.code.0)
}

fn text(m: &Message, attr: AttrType) -> Result<String> {
    Ok(TextAttribute::get_from_as(m, attr)?.text)
}

fn run(args: Args) -> Result<()> {
    let credentials = if args.z_rest {
        let user_id = args
            .username
            .as_deref()
            .ok_or("--zrest requires -u USER_ID")?;
        let secret = match args.shared_secret {
            Some(secret) => secret,
            None => rpassword::prompt_password("ZREST shared secret: ")?,
        };
        if secret.is_empty() {
            return Err("Shared secret must not be empty".into());
        }
        let now = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
        let (username, password) = rest_credentials(user_id, &secret, now);
        println!("ZREST username: {username}");
        println!("ZREST password: {password}");
        println!(
            "Valid for 3600 seconds; expires at Unix timestamp {}",
            now + 3600
        );
        Some((username, password))
    } else {
        match args.username {
            Some(username) => Some((
                username,
                match args.password {
                    Some(password) => password,
                    None => rpassword::prompt_password("STUN password: ")?,
                },
            )),
            None => None,
        }
    };
    // The crate expects SASL-prepared credentials. ASCII printable credentials
    // need no normalization; reject other input rather than silently mis-hash it.
    if let Some((username, password)) = &credentials {
        if !username
            .bytes()
            .chain(password.bytes())
            .all(|b| (0x20..=0x7e).contains(&b))
        {
            return Err("This minimal CLI supports printable ASCII credentials only".into());
        }
    }
    let (host, port) = endpoint(&args.host, args.port)?;
    let addresses: Vec<_> = (host.as_str(), port).to_socket_addrs()?.collect();
    let server = select_address(&addresses, args.ipv4, args.ipv6)?;
    let socket = UdpSocket::bind(if server.is_ipv4() {
        "0.0.0.0:0"
    } else {
        "[::]:0"
    })?;
    socket.connect(server)?;
    let timeout = Duration::from_secs(args.timeout);
    println!("Server: {server} (UDP)");
    if args.verbose {
        println!("Local socket: {}", socket.local_addr()?);
    }
    let mut response = exchange(&socket, &request(None)?, timeout, args.verbose)?;
    if response.typ == BINDING_SUCCESS {
        if credentials.is_none() {
            println!("OK: anonymous Binding Success");
            return print_address(&response);
        }
        return Err("Server accepted an anonymous Binding request; secure-stun authentication was not verified".into());
    }
    let (username, password) =
        credentials.ok_or("Server requires authentication; provide --username")?;
    if error_code(&response)? != 401 {
        return Err(format!(
            "Expected 401 challenge, received STUN error {}",
            error_code(&response)?
        )
        .into());
    }
    let realm = text(&response, ATTR_REALM)?;
    let mut nonce = text(&response, ATTR_NONCE)?;
    println!("401 challenge received; realm: {realm}");
    let integrity =
        MessageIntegrity::new_long_term_integrity(username.clone(), realm.clone(), password);
    for _ in 0..3 {
        let message = request(Some((&username, &realm, &nonce, &integrity)))?;
        response = exchange(&socket, &message, timeout, args.verbose)?;
        if response.typ == BINDING_SUCCESS {
            integrity.check(&mut response)?;
            println!("OK: authenticated Binding Success; MESSAGE-INTEGRITY verified");
            return print_address(&response);
        }
        let code = error_code(&response)?;
        if code != 438 {
            return Err(format!("Authenticated Binding rejected: STUN {code}").into());
        }
        // coturn may return an unsigned stale-nonce challenge.
        if response.contains(ATTR_MESSAGE_INTEGRITY) {
            integrity.check(&mut response)?;
        }
        nonce = text(&response, ATTR_NONCE)?;
        println!("438 stale nonce; retrying with new nonce");
    }
    Err("Too many stale-nonce challenges".into())
}

fn rest_credentials(user_id: &str, secret: &str, now: u64) -> (String, String) {
    let username = format!("{}:{user_id}", now + 3600);
    let key = hmac::Key::new(hmac::HMAC_SHA1_FOR_LEGACY_USE_ONLY, secret.as_bytes());
    let password = STANDARD.encode(hmac::sign(&key, username.as_bytes()).as_ref());
    (username, password)
}

fn select_address(addresses: &[SocketAddr], ipv4: bool, ipv6: bool) -> Result<SocketAddr> {
    addresses
        .iter()
        .copied()
        .find(|address| (!ipv4 || address.is_ipv4()) && (!ipv6 || address.is_ipv6()))
        .ok_or_else(|| "No server address available for the requested IP version".into())
}

fn endpoint(input: &str, port: Option<u16>) -> Result<(String, u16)> {
    let input = if input.starts_with("stun:") || input.starts_with("stuns:") {
        input.to_owned()
    } else if input.parse::<std::net::Ipv6Addr>().is_ok() {
        format!("stun:[{input}]")
    } else {
        format!("stun:{input}")
    };
    let uri = stun::uri::Uri::parse_uri(&input)?;
    if uri.scheme != "stun" {
        return Err("Only stun: over UDP is supported".into());
    }
    Ok((uri.host, port.or(uri.port).unwrap_or(3478)))
}

fn print_address(response: &Message) -> Result<()> {
    let mut address = XorMappedAddress::default();
    address.get_from(response)?;
    println!(
        "Mapped address: {}",
        SocketAddr::new(address.ip, address.port)
    );
    Ok(())
}

fn main() {
    if let Err(error) = run(Args::parse()) {
        eprintln!("Error: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generates_one_hour_rest_credentials() {
        let (username, password) = rest_credentials("alice", "test-secret", 1000);
        assert_eq!(username, "4600:alice");
        // Independently computed with openssl dgst -sha1 -hmac.
        assert_eq!(password, "tR9ktVPts7ZT1hFlY5TjKB3ZUGw=");
        assert!(Args::try_parse_from(["stun", "--zrest", "localhost"]).is_err());
        assert!(Args::try_parse_from([
            "stun",
            "--zrest",
            "localhost",
            "-u",
            "alice",
            "-w",
            "password"
        ])
        .is_err());
        assert!(Args::try_parse_from([
            "stun",
            "--zrest",
            "localhost",
            "-u",
            "alice",
            "--shared-secret",
            "secret"
        ])
        .is_ok());
    }

    #[test]
    fn selects_requested_ip_version() {
        let v6: SocketAddr = "[::1]:3478".parse().unwrap();
        let v4: SocketAddr = "127.0.0.1:3478".parse().unwrap();
        assert_eq!(select_address(&[v6, v4], true, false).unwrap(), v4);
        assert_eq!(select_address(&[v4, v6], false, true).unwrap(), v6);
        assert_eq!(select_address(&[v6, v4], false, false).unwrap(), v6);
        assert!(select_address(&[v6], true, false).is_err());
        assert!(select_address(&[v4], false, true).is_err());
        assert!(Args::try_parse_from(["stun", "-4", "-6", "localhost"]).is_err());
    }

    #[test]
    fn parses_stun_endpoints() {
        assert_eq!(
            endpoint("stun:stun.l.google.com:19302", None).unwrap(),
            ("stun.l.google.com".into(), 19302)
        );
        assert_eq!(
            endpoint("localhost:19302", Some(3478)).unwrap(),
            ("localhost".into(), 3478)
        );
        assert_eq!(endpoint("::1", None).unwrap(), ("::1".into(), 3478));
        assert!(endpoint("stuns:localhost", None).is_err());
    }

    #[test]
    fn anonymous_binding_succeeds_without_credentials() {
        let server = UdpSocket::bind("127.0.0.1:0").unwrap();
        server
            .set_read_timeout(Some(Duration::from_secs(3)))
            .unwrap();
        let port = server.local_addr().unwrap().port();
        let peer = std::thread::spawn(move || {
            let mut buffer = [0; 2048];
            let (n, addr) = server.recv_from(&mut buffer).unwrap();
            let mut request = Message::new();
            request.unmarshal_binary(&buffer[..n]).unwrap();
            assert!(!request.contains(ATTR_USERNAME));
            let mut reply = Message::new();
            reply.typ = BINDING_SUCCESS;
            reply.transaction_id = request.transaction_id;
            reply.write_header();
            XorMappedAddress {
                ip: addr.ip(),
                port: addr.port(),
            }
            .add_to(&mut reply)
            .unwrap();
            server.send_to(&reply.raw, addr).unwrap();
        });
        let result = run(Args {
            z_rest: false,
            shared_secret: None,
            verbose: false,
            ipv4: false,
            ipv6: false,
            host: format!("stun:127.0.0.1:{port}"),
            port: None,
            username: None,
            password: None,
            timeout: 2,
        });
        peer.join().unwrap();
        result.unwrap();
    }

    // Exercise the complete two-transaction exchange against a loopback peer.
    fn binding_test(mode: &str) -> bool {
        let server = UdpSocket::bind("127.0.0.1:0").unwrap();
        server
            .set_read_timeout(Some(Duration::from_secs(3)))
            .unwrap();
        let port = server.local_addr().unwrap().port();
        let mode = mode.to_owned();
        let peer = std::thread::spawn(move || {
            let receive = || {
                let mut buf = [0; 2048];
                let (n, addr) = server.recv_from(&mut buf).unwrap();
                let mut m = Message::new();
                m.unmarshal_binary(&buf[..n]).unwrap();
                (m, addr)
            };
            let (initial, addr) = receive();
            assert!(!initial.contains(ATTR_MESSAGE_INTEGRITY));
            let mut challenge = Message::new();
            challenge.typ = BINDING_ERROR;
            challenge.transaction_id = initial.transaction_id;
            challenge.write_header();
            ErrorCodeAttribute {
                code: stun::error_code::CODE_UNAUTHORIZED,
                reason: b"Unauthorized".to_vec(),
            }
            .add_to(&mut challenge)
            .unwrap();
            TextAttribute::new(ATTR_REALM, "test-realm".into())
                .add_to(&mut challenge)
                .unwrap();
            TextAttribute::new(ATTR_NONCE, "test-nonce".into())
                .add_to(&mut challenge)
                .unwrap();
            server.send_to(&challenge.raw, addr).unwrap();
            let (mut authenticated, second_addr) = receive();
            assert_eq!(addr, second_addr);
            assert_eq!(text(&authenticated, ATTR_USERNAME).unwrap(), "test-user");
            assert_eq!(text(&authenticated, ATTR_NONCE).unwrap(), "test-nonce");
            let key = MessageIntegrity::new_long_term_integrity(
                "test-user".into(),
                "test-realm".into(),
                "test-password".into(),
            );
            key.check(&mut authenticated).unwrap();
            let mut success = Message::new();
            success.typ = BINDING_SUCCESS;
            success.transaction_id = authenticated.transaction_id;
            success.write_header();
            XorMappedAddress {
                ip: addr.ip(),
                port: addr.port(),
            }
            .add_to(&mut success)
            .unwrap();
            if mode != "missing" {
                let signing_key = if mode == "wrong" {
                    MessageIntegrity::new_short_term_integrity("wrong".into())
                } else {
                    key
                };
                signing_key.add_to(&mut success).unwrap();
            }
            FINGERPRINT.add_to(&mut success).unwrap();
            server.send_to(&success.raw, addr).unwrap();
        });
        let result = run(Args {
            z_rest: false,
            shared_secret: None,
            verbose: false,
            ipv4: false,
            ipv6: false,
            host: "127.0.0.1".into(),
            port: Some(port),
            username: Some("test-user".into()),
            password: Some("test-password".into()),
            timeout: 2,
        });
        peer.join().unwrap();
        result.is_ok()
    }

    #[test]
    fn authenticated_binding_succeeds() {
        assert!(binding_test("valid"));
    }

    #[test]
    fn rejects_invalid_response_integrity() {
        assert!(!binding_test("wrong"));
    }

    #[test]
    fn rejects_unsigned_success() {
        assert!(!binding_test("missing"));
    }
}
