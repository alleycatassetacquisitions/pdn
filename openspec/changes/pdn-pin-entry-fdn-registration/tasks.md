## 1. PDN

- [x] 1.1 Registration screen takes a 4-digit PIN and sends it to the cabled FDN over a reliable channel
- [x] 1.2 Fetch user applies the FDN's reply only from the cabled FDN and only for the PIN it sent
- [x] 1.3 Demo PIN `0000`: role picker, then the hub; demo matches are not stored

## 2. FDN

- [x] 2.1 Validate the PIN, look it up with an 8 s deadline, reply over a reliable channel
- [x] 2.2 Registration screen while an unprovisioned PDN is Connected

## 3. Verification

- [x] 3.1 Native tests for request, reply, resend, cabled gating, unprovisioned detection
- [ ] 3.2 Hardware: register a PDN through a cabled FDN, and the demo path
