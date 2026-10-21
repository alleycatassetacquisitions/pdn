## ADDED Requirements

### Requirement: Batch size is validated before an upload is queued
The client SHALL accept batches of 1 to 100 matches. For any other count, it SHALL report an error through the error callback and SHALL NOT queue an HTTP request.

#### Scenario: Count outside 1–100
- **WHEN** an upload is requested with 0 or 101 matches
- **THEN** the error callback receives `WirelessError::INVALID_STATE`
- **AND** no HTTP request is queued

### Requirement: Upload uses the protobuf match endpoint
The client SHALL queue a `POST` request to `/quickdraw-matches` with content type `application/protobuf` and a body that is an encoded `WriteQuickdrawMatchesRequest`.

#### Scenario: Request shape
- **WHEN** an upload is requested with one match
- **THEN** the queued request has method `POST`, path `/quickdraw-matches`, content type `application/protobuf`, and a payload that decodes as `WriteQuickdrawMatchesRequest` with one match

### Requirement: Matches are encoded with every required field present
For each match, the encoder SHALL write the match UUID, both player UUIDs, the winner UUID, both draw times, and the occurrence timestamp. Draw times and the occurrence timestamp SHALL be marked present even when zero. A team UUID SHALL be written only when it is not nil. Records hold UUIDs as 16 bytes; the encoder SHALL write them as lowercase 8-4-4-4-12 text.

#### Scenario: Zero draw time
- **WHEN** a match has `playerADrawTimeMs` of 0
- **THEN** the decoded `QuickdrawMatch` has `player_a_draw_time_ms` present with value 0

#### Scenario: Unassigned team
- **WHEN** a match record has a nil team for player B
- **THEN** the decoded `QuickdrawMatch` has `player_b_team_id` absent

#### Scenario: Assigned teams
- **WHEN** a match record has teams for both players
- **THEN** the decoded `QuickdrawMatch` has both team UUIDs present and equal to the record's values as lowercase text

### Requirement: Fully accepted batches report no rejections
When the server accepts every match, it responds 204 with an empty body. On a successful response with an empty body, or a body that decodes to zero rejections, the client SHALL invoke the success callback with a result that reports full acceptance and no rejections.

#### Scenario: 204 No Content
- **WHEN** the upload succeeds with an empty response body
- **THEN** the success callback receives a result that reports full acceptance and has no rejections
- **AND** the rejection callback is not invoked

### Requirement: Partial rejections are decoded per entry
On a successful response with a non-empty body, the client SHALL decode it as `WriteQuickdrawMatchesResponse`. The client SHALL deliver one rejection per entry to the rejection callback, in response order, each with the match index, submitted match ID, up to two field names, and error code, and then invoke the success callback with the rejection count. An error code the client does not recognize SHALL be reported as unknown, and the client SHALL keep its raw numeric value.

#### Scenario: One rejected entry
- **WHEN** the response rejects index 2 with code `SAME_PLAYER` and fields `player_a_id` and `player_b_id`
- **THEN** the result does not report full acceptance and has a rejection count of 1
- **AND** the rejection callback receives match index 2, the submitted match ID, fields `["player_a_id", "player_b_id"]`, and code `SAME_PLAYER`

#### Scenario: Full batch rejected
- **WHEN** the response rejects all 100 entries
- **THEN** the rejection callback receives 100 rejections in response order

#### Scenario: Future error code
- **WHEN** a rejection carries numeric code 42
- **THEN** the rejection's code is reported as unknown and its raw code value is 42

### Requirement: Undecodable responses are reported as errors
If a non-empty successful response body cannot be decoded as `WriteQuickdrawMatchesResponse`, the client SHALL invoke the error callback with `WirelessError::INVALID_RESPONSE` and SHALL NOT invoke the rejection or success callbacks, so the caller keeps every match in the batch.

#### Scenario: Garbage body
- **WHEN** the upload succeeds with a body that is not a valid `WriteQuickdrawMatchesResponse`
- **THEN** the error callback receives `WirelessError::INVALID_RESPONSE`

#### Scenario: Rejection exceeds a device limit
- **WHEN** a rejection lists more than two fields
- **THEN** the response is treated as undecodable and no rejection is delivered

#### Scenario: Malformed rejected match ID
- **WHEN** any rejection's `match_id` is not 8-4-4-4-12 hexadecimal text
- **THEN** the response is treated as undecodable and no rejection is delivered

### Requirement: Transport errors are forwarded unchanged
If the HTTP layer reports an error, including a non-2xx status for a batch-wide rejection, the client SHALL pass the `WirelessErrorInfo` to the error callback without modification.

#### Scenario: Batch-wide conflict
- **WHEN** the HTTP layer reports an error for the upload request
- **THEN** the error callback receives the same `WirelessErrorInfo`
