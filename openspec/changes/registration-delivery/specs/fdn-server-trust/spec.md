## ADDED Requirements

### Requirement: Server key pin in the FDN build

An FDN image SHALL carry the SHA-256 of the server's public key, the DER SubjectPublicKeyInfo of its leaf certificate, as `64` hexadecimal characters set in build configuration next to the server address. An image whose pin is missing or is not `64` hexadecimal characters SHALL trust no server and SHALL send no request.

#### Scenario: Image with no pin trusts no server
- **WHEN** an FDN built with no server key pin starts an upload cycle
- **THEN** the request fails at once as untrusted, before the radio joins Wi-Fi, and the records stay stored

#### Scenario: Malformed pin trusts no server
- **WHEN** an FDN is built with a pin of `63` or `65` hexadecimal characters, or with a character that is not hexadecimal
- **THEN** it trusts no server and sends no request

### Requirement: Plain HTTP is never trusted

The FDN SHALL send a request only over HTTPS to the pinned key. A server address that does not start with `https://`, including one with no scheme, SHALL make every request fail at once as untrusted, with no network traffic. Trust SHALL be fixed by the build configuration; no response carries its own trust verdict.

#### Scenario: http address sends nothing
- **WHEN** an FDN built with a well-formed pin and the server address `http://localhost:3000` starts an upload cycle
- **THEN** the request fails at once as untrusted and nothing is sent

#### Scenario: Address without a scheme sends nothing
- **WHEN** an FDN is built with a server address that has no scheme
- **THEN** the FDN does not add one and every request fails at once as untrusted

### Requirement: The pinned key is the server's identity

Over HTTPS the FDN SHALL trust the server iff its leaf certificate's key hashes to the pin, whatever chain comes with it. Public certificate authorities SHALL confer no trust. The FDN SHALL NOT check host names or validity dates, and SHALL still send SNI. A request to an untrusted server SHALL fail at once, without retry and before any of its response is used, and the FDN SHALL log the SHA-256 of the key presented.

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

The FDN SHALL follow no redirect: an answer with a `3xx` status SHALL fail the request.

#### Scenario: Redirect fails the request
- **WHEN** the trusted server answers a request with a `302` redirect
- **THEN** the FDN does not follow it and the request fails

### Requirement: Every server request uses the same trust

Every request the FDN sends to the server, today the match and crash uploads and later the player lookup, SHALL use one HTTP client, server address and pin. An upload that is refused as untrusted, reaches an untrusted server or is answered with a redirect SHALL fail and keep its records for the next upload cycle.

#### Scenario: Upload to an untrusted server keeps the records
- **WHEN** an upload cycle reaches a server whose key does not hash to the pin
- **THEN** the upload fails and the FDN keeps every pending record for the next cycle

#### Scenario: Lookup uses the same trust
- **WHEN** a later FDN feature sends a request to the server
- **THEN** it goes through the same client and is refused, trusted or redirected by the same rules as an upload

### Requirement: Rotating the server key

An FDN SHALL hold one pin and SHALL offer no way to change it at run time. Changing the server's key SHALL require reflashing every FDN with an image carrying the new pin. Renewing the certificate for the same key SHALL need no reflash.

#### Scenario: New server key without a reflash
- **WHEN** the server starts presenting a new key and an FDN still carries the old pin
- **THEN** that FDN trusts no server and uploads nothing until it is reflashed with the new pin

### Requirement: Simulator makes no trusted request

The native simulator SHALL make no HTTPS request and SHALL treat no response as coming from a trusted server. The comparison of a presented key's hash with the pin, and the rule for which configurations may send, SHALL be testable without a network.

#### Scenario: Simulator response is never trusted
- **WHEN** the native simulator runs
- **THEN** it has no HTTP client, sends no server request and trusts no response
