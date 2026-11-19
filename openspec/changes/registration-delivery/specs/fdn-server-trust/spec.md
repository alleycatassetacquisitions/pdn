## ADDED Requirements

### Requirement: Server verified by public CA and host name

Over HTTPS the FDN SHALL trust the server only when its certificate chains to a CA in the public CA bundle built into ESP-IDF, is valid for the host name in the server address and is within its validity dates. The FDN SHALL hold no pinned key and no other trust setting. A request whose server fails verification SHALL fail at once as untrusted, without retry and before any of its response is used.

#### Scenario: Certificate from a public CA for the host is trusted
- **WHEN** the server presents an unexpired certificate for the configured host name, issued under a CA in the bundle
- **THEN** the FDN trusts the server and the request proceeds

#### Scenario: Certificate for another host is not trusted
- **WHEN** the server presents a public CA certificate valid for another host name
- **THEN** the request fails at once as untrusted, with no second attempt

#### Scenario: Self-signed or expired certificate is not trusted
- **WHEN** the server presents a self-signed certificate, or one past its expiry date
- **THEN** the request fails at once as untrusted

### Requirement: Plain HTTP is never used

The FDN SHALL send a request only to a server address starting with `https://`. Any other address, including one with no scheme, SHALL make every request fail at once as untrusted, before the radio joins Wi-Fi and with no network traffic. No response carries a trust verdict of its own: each was verified or its request failed.

#### Scenario: http address sends nothing
- **WHEN** an FDN built with the server address `http://localhost:3000` starts an upload cycle
- **THEN** the request fails at once as untrusted, the radio stays on ESP-NOW and nothing is sent

#### Scenario: Address without a scheme sends nothing
- **WHEN** an FDN is built with a server address that has no scheme
- **THEN** the FDN does not add one and every request fails at once as untrusted

### Requirement: Redirects refused

The FDN SHALL follow no redirect, since one could lead away from `https://`: an answer with a `3xx` status SHALL fail the request.

#### Scenario: Redirect fails the request
- **WHEN** the server answers a request with a `302` redirect
- **THEN** the FDN does not follow it and the request fails

### Requirement: Clock set before the first TLS connection

The FDN SHALL start no TLS connection until its wall clock is set (2025 or later), since certificate dates are checked against it. A request SHALL wait at most `10000 ms` with Wi-Fi up for the clock and then fail, not as untrusted. An upload cycle that starts with the clock unset SHALL add that wait to its `15,000 ms` window.

#### Scenario: No clock, no TLS request
- **WHEN** an upload cycle starts after a cold boot and SNTP has not set the clock
- **THEN** no TLS connection is made, the request fails after the wait and every record stays stored

#### Scenario: Clock set, request proceeds
- **WHEN** SNTP sets the clock while a request waits
- **THEN** the request is sent at once

#### Scenario: First cycle after a cold boot gets the clock wait
- **WHEN** an upload cycle starts before SNTP has set the clock
- **THEN** the cycle keeps Wi-Fi for up to `25,000 ms` before it gives the radio back

### Requirement: Every server request uses the same trust

Every request the FDN sends to the server, today the match and crash uploads and later the player lookup, SHALL use one HTTP client, server address and verification. An upload that is refused, reaches an untrusted server, waits out the clock or is answered with a redirect SHALL fail and keep its records for the next upload cycle.

#### Scenario: Upload to an untrusted server keeps the records
- **WHEN** an upload cycle reaches a server that fails certificate verification
- **THEN** the upload fails and the FDN keeps every pending record for the next cycle

#### Scenario: Lookup uses the same trust
- **WHEN** a later FDN feature sends a request to the server
- **THEN** it goes through the same client and is refused, verified, held or redirected by the same rules as an upload

### Requirement: Simulator makes no trusted request

The native simulator SHALL make no HTTPS request and SHALL treat no response as coming from a verified server. The rule for which addresses may be sent to and the clock hold SHALL be testable without a network.

#### Scenario: Simulator response is never trusted
- **WHEN** the native simulator runs
- **THEN** it has no HTTP client, sends no server request and trusts no response
