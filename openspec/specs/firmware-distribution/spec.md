# firmware-distribution Specification

## Purpose

A device flashed over USB can hand the firmware it is running to every other PDN in radio range, so a fleet is updated without plugging each device in. This spec covers distribution from the operator's trigger to a receiver confirming the new image. Signing the image belongs to the build; the partition layout is stated here because distribution depends on it.

Terms:

- **Seed**: the one device broadcasting an image. It always distributes the image it is running, so a seed is evidence the image boots.
- **Receiver**: a device writing that image to its inactive slot.
- **Slot**: an `ota_0` or `ota_1` application partition. A device runs one and writes the other.
- **App description**: the image's `esp_app_desc_t`, 32 bytes into the image.
- **Image identity**: the app description's `app_elf_sha256`.
- **Build time**: the app description's `date` and `time`, stamped in UTC on every build.

## Requirements

### Requirement: Fault model

Distribution SHALL remain correct when:

- any frame is lost (broadcasts are neither acknowledged nor retried by the radio);
- a frame reaches every device in range, including FDNs and devices not updating;
- a receiver resets or loses power at any moment, losing its transfer state;
- any device in range transmits forged frames.

#### Scenario: Radio success is not receipt
- **WHEN** the seed's radio reports a chunk was transmitted
- **THEN** the seed draws no conclusion about any receiver holding that chunk

#### Scenario: Receiver reboots mid-transfer
- **WHEN** a receiver resets after writing part of an image
- **THEN** it starts over from the next copy of chunk 0 it hears, with an erased slot

### Requirement: Partition layout

A device SHALL carry two application slots of equal size and an `otadata` partition. `ota_0` SHALL begin at `0x010000` and `ota_1` immediately after it, each `0x3E0000` bytes.

#### Scenario: Slot is large enough for the image it receives
- **WHEN** chunk 0 describes an image of up to `0x3E0000` bytes
- **THEN** it fits the inactive slot without the seed reducing it

#### Scenario: Image larger than a slot
- **WHEN** chunk 0 describes an image longer than the inactive slot
- **THEN** the receiver does not start receiving it

### Requirement: Commit and rollback

A receiver whose image the store accepts SHALL set it as the boot partition pending confirmation, and restart. The new image SHALL confirm itself only after its main loop has run for 10 seconds, counted from the loop's first pass so that a slow startup cannot use them up. Until then the main task SHALL be watched by a task watchdog that panics after 30 seconds. A device that crashes, hangs or loses power before confirming SHALL boot its previous image on the following boot, with no operator.

An image the store refuses, or a receive that is abandoned, SHALL leave the slot's first sector erased, so the bootloader cannot fall back to what was written. The bootloader checks no signature at boot, and a forger can leave an intact unsigned image by declaring it longer than it is and letting the receive stall.

#### Scenario: New image runs
- **WHEN** a device boots a pending image and its main loop runs for 10 seconds
- **THEN** it confirms the image and the previous slot becomes the spare

#### Scenario: New image crashes on boot
- **WHEN** a device boots a pending image and resets before confirming
- **THEN** the bootloader selects the previous slot and the device runs its old image

#### Scenario: New image hangs before confirming
- **WHEN** a device boots a pending image that hangs
- **THEN** the watchdog resets it and the bootloader boots the previous slot

### Requirement: Eligibility

A PDN SHALL take an image only while its pairing-code screen is up; whoever is on that screen is not playing, so an update may cut short a code entry or a cabled registration. No other screen takes one: on game Idle a registered player's registration and stats live only in RAM, and the restart would lose them.

#### Scenario: Registered player on game Idle
- **WHEN** chunk 0 arrives at a PDN on game Idle
- **THEN** it does not start receiving

#### Scenario: Device in a match
- **WHEN** chunk 0 arrives at a device mid-match
- **THEN** it does not start receiving

#### Scenario: Leaving the pairing screen mid-transfer
- **WHEN** the pairing-code screen gives way to another during a receive
- **THEN** the receive is dropped and the device keeps its current image

### Requirement: Operator trigger

A device SHALL become a seed only when its holder keeps the secondary button down for 5 seconds on the PDN's pairing-code screen, and SHALL NOT become one in response to any frame. A hold already under way when the screen appears SHALL NOT count until the button has been released. Seeding never restarts the seed itself.

Every PDN starts on the pairing-code screen, so any PDN can seed after a restart.

#### Scenario: Remote start refused
- **WHEN** a device hears another seed's frames
- **THEN** it does not seed

#### Scenario: Hold carried onto the screen
- **WHEN** the secondary button is already held as the pairing-code screen appears
- **THEN** no seed starts until the button is released and held again for 5 seconds

#### Scenario: Hold on game Idle
- **WHEN** a holder keeps the secondary button down on game Idle
- **THEN** the device does not seed

### Requirement: Frame

Distribution SHALL use one frame type and fit it in one ESP-NOW v2 frame, and its layout, packet type and ESP-NOW channel SHALL never change, since only older builds receive it: the image identity, the image's length, a chunk index, and up to 1400 bytes of the image at `index × 1400`. Chunk 0 SHALL carry the image's app description, which sits 32 bytes into the image.

#### Scenario: A full chunk fits one frame
- **WHEN** a seed sends a chunk carrying 1400 bytes of image
- **THEN** the whole frame fits one ESP-NOW v2 frame and is never split

### Requirement: Seeding

A seed SHALL broadcast every chunk of its running image, in order, for three passes, and SHALL also send chunk 0 once a second for the whole run. It SHALL queue a frame only when the radio has reported on the previous one. A frame the radio refuses counts as lost. The run SHALL end after the third pass, whoever is listening.

#### Scenario: Nobody is listening
- **WHEN** a seed runs with no receiver in range
- **THEN** it sends its three passes and returns to the screen it started from

### Requirement: Receiving

A device SHALL start receiving an image only from chunk 0, only while eligible, and only if the app description it carries passes `Order`. That judgement SHALL be made before anything is erased. A device whose running image is not yet confirmed SHALL NOT start receiving.

ESP-NOW frames waiting to be handled SHALL be bounded at 16 KB of payload across every packet type, so a device beside a seed cannot run out of memory; frames past the bound are dropped.

A receiver SHALL:

- write each chunk the first time it hears it, at its own offset, and only if its length is exactly what the image's length implies;
- ignore chunks of any other image while receiving one;
- drop the receive when 5 seconds pass without any chunk of its image, or when the device stops being eligible;
- hand the image to the store once every chunk is written;
- ignore an image the store refused for as long as that image's frames keep arriving, and hear it again once they have stopped for 5 seconds.

#### Scenario: Device powered on mid-run
- **WHEN** a device becomes eligible during a run
- **THEN** it starts from the next chunk 0, which arrives within a second, and fills the chunks it missed from later passes if any remain

#### Scenario: Waiting a pass for missed chunks
- **WHEN** a receiver is missing chunks that come round only in the next pass
- **THEN** the chunks it already has keep the receive alive meanwhile

#### Scenario: Two seeds in range
- **WHEN** frames of two images interleave
- **THEN** a receiver writes only chunks of the image it started on

### Requirement: Authenticity

The store SHALL accept an assembled image only if ESP-IDF verifies it (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`, RSA-3072): intact, and signed with the key that signed the running image. Every PDN build SHALL be signed, since IDF refuses to start an unsigned image under this setting. FDN builds SHALL NOT be signed, so no PDN takes an FDN image.

IDF checks one signature against one key. The signing key can therefore never change over the air: rotating it, or retiring a leaked one, takes a USB flash of every device.

#### Scenario: Image signed with another key
- **WHEN** a receiver assembles an image signed with a key other than its own image's
- **THEN** the store refuses it, the image is left unbootable, and the device does not restart

### Requirement: Order

A device SHALL take an image only if its build time is later than the running image's. It SHALL be checked from chunk 0 before erasing, and again on the written image before it is made bootable. A build time that cannot be read SHALL count as not later.

#### Scenario: Older image
- **WHEN** chunk 0 describes an image built before the running one
- **THEN** the receiver neither erases nor receives anything

## Residual gaps

These are decisions, not oversights.

- **Chunks are unauthenticated.** A forger in range cannot install an image, but can spoil a run, or make receivers erase their spare slot and wait for chunks that never come.
- **A rolled-back image is taken again** while a seed of it is in range: the store does not remember which image failed.
- **The signing key is permanent** over the air (see `Authenticity`).
- **The key check is software.** An attacker who can write flash can replace both image and key; only Secure Boot v2 closes that, and it is irreversible on hand-built hardware.
- **The erase blocks the loop.** `esp_ota_begin` erases the image's length (~3.5 s for 2.2 MB) before returning.
- **A run occupies the channel**, about 80% of it for about 70 seconds.
- **Build time orders builds, not commits.** An older commit built later is taken as newer.
- **A fast build clock blocks later builds.** An image built on a host whose clock runs ahead is newer than every honest build until real time passes its stamp.
- **The receive bound covers game traffic.** Beside a seed, game frames past the 16 KB bound are dropped along with chunks.
- **The task watchdog is 30 seconds on PDNs.** Core 0's idle-task check takes 30 seconds, not 5, to reset a PDN whose core 0 is starved.
- **A pending image takes no update.** IDF refuses to open a slot until the running image confirms, so a device that just updated skips a newer run that starts within its first 10 seconds.
