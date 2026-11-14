## ADDED Requirements

### Requirement: Server key pin in the FDN build

An FDN image SHALL carry the SHA-256 of the server's public key, the DER SubjectPublicKeyInfo of its leaf certificate, as `64` hexadecimal characters set in build configuration next to the server address. An image whose pin is missing or is not `64` hexadecimal characters SHALL trust no server, so it registers nobody.

#### Scenario: Image with no pin trusts no server
- **WHEN** an FDN built with no server key pin looks up a tapped band
- **THEN** the request fails as from an untrusted server and the PDN gets the lookup-failure notification

#### Scenario: Malformed pin trusts no server
- **WHEN** an FDN is built with a pin of `63` hexadecimal characters
- **THEN** it trusts no server and registers nobody

### Requirement: The pinned key is the server's identity

Over HTTPS the FDN SHALL trust the server if and only if its leaf certificate's public key hashes to the pin, whatever chain comes with it. Public certificate authorities SHALL confer no trust. The FDN SHALL NOT check host names or validity dates, and SHALL still send SNI. A request to an untrusted server SHALL fail at once, without retry and before any part of its response is used, and the FDN SHALL log the SHA-256 of the key that server presented.

#### Scenario: Pinned key with a renewed certificate is trusted
- **WHEN** the server presents a new certificate for the same key, issued by no public certificate authority, for another host name and past its expiry date
- **THEN** the FDN trusts the server

#### Scenario: Public CA certificate with another key is not trusted
- **WHEN** the server presents a certificate from a public certificate authority, valid for the configured host name, whose key does not hash to the pin
- **THEN** the FDN does not trust the server and the request fails

#### Scenario: Leaf with another key under a server-supplied intermediate is not trusted
- **WHEN** the server presents a leaf certificate whose key does not hash to the pin, chained to an intermediate certificate the server itself sends
- **THEN** the FDN does not trust the server and the request fails

#### Scenario: Untrusted server is logged and not retried
- **WHEN** a request reaches a server whose key does not hash to the pin
- **THEN** the FDN logs the SHA-256 of the presented key and the request fails at once as untrusted, with no second attempt

### Requirement: Redirects refused

The FDN SHALL follow no redirect: an answer that redirects SHALL fail the request.

#### Scenario: Redirect fails the lookup
- **WHEN** the trusted server answers a lookup with a `302` redirect
- **THEN** the FDN does not follow it and treats the lookup as failed

### Requirement: Uploads use the same trust

Match uploads and crash uploads SHALL use the same HTTP client, server address and pin as the lookup. An upload to a server that is not trusted, or that answers with a redirect, SHALL fail as an upload to an unreachable server does, keeping its records for the next upload cycle.

#### Scenario: Upload to an untrusted server keeps the records
- **WHEN** an upload cycle reaches a server whose key does not hash to the pin
- **THEN** the upload fails and the FDN keeps every pending record for the next cycle

### Requirement: Rotating the server key

An FDN SHALL hold one pin and SHALL offer no way to change it at run time. Changing the server's key SHALL require reflashing every FDN with an image carrying the new pin. Renewing the certificate for the same key SHALL need no reflash.

#### Scenario: New server key without a reflash
- **WHEN** the server starts presenting a new key and an FDN still carries the old pin
- **THEN** that FDN trusts no server and registers nobody until it is reflashed with the new pin

### Requirement: Simulator makes no trusted request

The native simulator SHALL make no HTTPS request and SHALL report no response as coming from a trusted server, so it accepts no record key from a lookup. The comparison of a presented key's hash with the pin SHALL be testable without a network.

#### Scenario: Simulator response is never trusted
- **WHEN** the native simulator's HTTP driver answers any request
- **THEN** the response is not reported as coming from a trusted server
