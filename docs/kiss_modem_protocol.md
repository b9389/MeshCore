# MeshCore KISS Modem Protocol

Standard KISS TNC firmware for MeshCore LoRa radios. Compatible with any KISS client (Direwolf, APRSdroid, YAAC, etc.) for sending and receiving raw packets. MeshCore-specific extensions (cryptography, radio configuration, telemetry) are available through the standard SetHardware (0x06) command.

## Serial Configuration

115200 baud, 8N1, no flow control.

## Frame Format

Standard KISS framing per the KA9Q/K3MC specification.

| Byte | Name | Description |
|------|------|-------------|
| `0xC0` | FEND | Frame delimiter |
| `0xDB` | FESC | Escape character |
| `0xDC` | TFEND | Escaped FEND (FESC + TFEND = 0xC0) |
| `0xDD` | TFESC | Escaped FESC (FESC + TFESC = 0xDB) |

```
┌──────┬───────────┬──────────────┬──────┐
│ FEND │ Type Byte │ Data (escaped)│ FEND │
│ 0xC0 │  1 byte   │ 0-510 bytes  │ 0xC0 │
└──────┴───────────┴──────────────┴──────┘
```

### Type Byte

The type byte is split into two nibbles:

| Bits | Field | Description |
|------|-------|-------------|
| 7-4 | Port | Port number (0 for single-port TNC) |
| 3-0 | Command | Command number |

Maximum unescaped frame size: 512 bytes.

## Standard KISS Commands

### Host to TNC

| Command | Value | Data | Description |
|---------|-------|------|-------------|
| Data | `0x00` | Raw packet | Queue packet for transmission |
| TXDELAY | `0x01` | Delay (1 byte) | Transmitter keyup delay in 10ms units (default: 50 = 500ms) |
| Persistence | `0x02` | P (1 byte) | CSMA persistence parameter 0-255 (default: 63) |
| SlotTime | `0x03` | Interval (1 byte) | CSMA slot interval in 10ms units (default: 10 = 100ms) |
| TXtail | `0x04` | Delay (1 byte) | Post-TX hold time in 10ms units (default: 0) |
| FullDuplex | `0x05` | Mode (1 byte) | 0 = half duplex, nonzero = full duplex (default: 0) |
| SetHardware | `0x06` | Sub-command + data | MeshCore extensions (see below) |
| Return | `0xFF` | - | Exit KISS mode (no-op) |

### TNC to Host

| Type | Value | Data | Description |
|------|-------|------|-------------|
| Data | `0x00` | Raw packet | Received packet from radio |

Data frames carry raw packet data only, with no metadata prepended. The Data command payload is limited to 255 bytes to match the MeshCore maximum transmission unit (MAX_TRANS_UNIT); frames larger than 255 bytes are silently dropped. The KISS specification recommends at least 1024 bytes for general-purpose TNCs; this modem is intended for MeshCore packets only, whose protocol MTU is 255 bytes.

### CSMA Behavior

The TNC implements p-persistent CSMA for half-duplex operation:

1. When a packet is queued, monitor carrier detect
2. When the channel clears, generate a random value 0-255
3. If the value is less than or equal to P (Persistence), wait TXDELAY then transmit
4. Otherwise, wait SlotTime and repeat from step 1

Firmware `0B00` also adds firmware-owned low-rate data admission. ACK/control frames bypass this gate, but low-priority data frames receive a randomized release time before CSMA. If the radio observes a busy channel while data is at the head of the queue, the data frame retreats with another randomized busy-channel backoff. This does not create a hidden-terminal solution by itself, but it keeps independent hosts from immediately colliding after each board's local queue says it has capacity.

Firmware `0D00` makes that data gate adaptive. Busy-channel retreats and backpressure increase a bounded local congestion score, which widens future randomized data-admission and busy-retreat windows. Successful data transmissions decay the score. The score is local-only and does not require a coordinator; hosts should treat it as a self-throttle signal, not as proof of end-to-end delivery.

Firmware `0E00` lets the host report delivery/ACK outcomes back into the same local score with `ReportAdmissionFeedback`. Delivered or ACKed frames decay the score; lost frames and ACK timeouts raise it. Firmware still does not parse Cinder message semantics itself, so this is an explicit host-feedback path for bench/gateway controllers.

Firmware `0F00` adds `ResetAdmissionState` so bench controllers can clear carried admission counters, feedback counters, local congestion score, and pending admission timers before a run. The command is accepted only while the firmware TX path is idle; it does not flush queued traffic.

Firmware `1000` keeps the wire format stable but changes data-admission policy. Low-priority data frames now use at least one estimated frame airtime as their zero-score randomized release floor, congestion score decays with time, and busy/drop/lost-feedback score increases are less abrupt. The goal is to move board-local behavior closer to the host-side `channel-airtime` baseline without changing the modem profile.

Firmware `1100` widens the zero-score data-admission contention window to the existing `8000 ms` cap. This is intentionally conservative for max-payload LoRa tests; it should reduce clustered first transmissions from independent nodes before adding any explicit neighbor coordination.

Firmware `1200` adds runtime admission configuration. Hosts can set and read the firmware's data-admission window, busy-retreat window, and congestion-score decay interval without reflashing. The command is intended for bench sweeps and gateway-driven policy experiments; firmware still enforces hard caps and only accepts changes while idle.

Firmware `1300` adds the first board-local overheard-channel input. Each received packet records an observed RX guard for low-priority data admission; ACK/control traffic keeps priority bypass. This gives the scheduler a short post-RX quiet period based on real packets heard by the radio, not only local queued TX state.

Firmware `1400` makes that observed-RX guard runtime-tunable through AdmissionConfig v2. Firmware `1500` keeps the same wire format but changes policy: observed RX no longer hard-blocks data. Instead, the active observed-RX window is added to the randomized data-admission floor, preserving ACK/control bypass while testing overheard traffic as a softer channel-busy bias. Firmware `1600` changes the same v2 window into randomized observed-RX retreat: newly queued data may add randomized jitter while the window is active, and already queued data can be retimed when the board overhears another data packet. The default observed-RX behavior is disabled (`0-0 ms @ 0%`) until a live sweep proves a better nonzero setting.

Firmware `1700` keeps the same wire payloads but starts parsing Cinder native data-frame traffic classes. ACK/NACK remain highest priority, explicit native control frames and `Control`/`StoreControl` data frames bypass the low-rate data gate, `Interactive` keeps the normal data lane, and `Telemetry`/`Bulk` use lower-priority admission with stricter queue/airtime watermarks. Unknown future data classes are treated as lower-priority telemetry rather than interactive. This is the first traffic-aware firmware controller step after the observed-RX-only experiments.

Firmware `1800` keeps the same wire and stats payloads but makes the randomized data-admission and busy-retreat windows traffic-class aware. Interactive data uses a shorter zero-score contention cap based on estimated airtime, telemetry uses a wider floor/cap, and bulk uses the widest floor. Local congestion score can still stretch any data-like class toward the existing hard caps. This is intended to make mixed-class runs show policy separation instead of only queue-priority separation.

Firmware `1900` keeps the same host-visible payload formats and adds class-specific host-feedback pressure. The firmware records recently transmitted native data message IDs, maps `ReportAdmissionFeedback` outcomes back to the transmitted traffic class, and widens only that class's randomized admission/busy windows after lost or ACK-timeout feedback. Delivered and ACKed feedback decays that class pressure. This prevents telemetry failures from globally stretching interactive admission while still letting low-priority traffic back off under loss.

Firmware `1A00` keeps the same host-visible payload formats, bounds class-specific feedback pressure more tightly, and protects ACK turns with a short data-only post-RX guard. Firmware `1B00` keeps those behaviors and makes congestion score class-specific too. Busy-channel, drop, loss, and ACK-timeout pressure now stretches only the affected traffic class; the existing stats score remains the interactive reference value for compatibility. Firmware `1C00` changes ACK-turn protection for native Cinder data from a broad post-RX data delay to handle-aware neighbor busy memory. Observed native data marks source/destination handles busy briefly; queued native data is delayed only when its destination handle matches that busy set. Unknown or non-native data still uses the broad ACK-turn fallback. Firmware `1D00` lets queued ACK/NACK frames bypass the post-TX channel guard, reports neighbor-busy as its own scheduler defer reason, and appends neighbor-busy/ACK-bypass counters to `Stats`. Firmware `1E00` does not change MAC policy; it appends RadioLib receive diagnostics so aggregate receive-error deltas can be tied to concrete `readData` or `startReceive` error codes. Firmware `1F00` keeps the same stats payload and separates first-send admission from retry pressure: host loss/ACK-timeout feedback no longer raises the class congestion score for new message IDs, while retransmitted native data message IDs still inherit class-specific feedback pressure. Firmware `2000` appends receive-error category counters so CRC mismatch, damaged LoRa header, and other read failures can be trended separately. Firmware `2100` keeps the wire format stable and feeds categorized receive-error deltas plus lost/ACK-timeout feedback back into first-send data admission as bounded channel-pressure evidence. Firmware `2200` narrows that experiment to receive-error-only channel pressure after two-node testing showed generic loss feedback was too noisy for first-send admission. Firmware `2300` keeps the same host-visible payloads and adds a randomized shared-channel cooldown when CRC or damaged-header deltas appear, so already queued data can be retimed after collision evidence instead of only future first sends seeing a wider admission window.

In full-duplex mode, CSMA is bypassed and packets transmit after TXDELAY.

## SetHardware Extensions (0x06)

MeshCore-specific functionality uses the standard KISS SetHardware command. The first byte of SetHardware data is a sub-command. Standard KISS clients ignore these frames.

### Frame Format

```
┌──────┬──────┬─────────────┬──────────────┬──────┐
│ FEND │ 0x06 │ Sub-command  │ Data (escaped)│ FEND │
│ 0xC0 │      │   1 byte    │   variable   │ 0xC0 │
└──────┴──────┴─────────────┴──────────────┴──────┘
```

### Request Sub-commands (Host to TNC)

| Sub-command | Value | Data |
|-------------|-------|------|
| GetIdentity | `0x01` | - |
| GetRandom | `0x02` | Length (1 byte, 1-64) |
| VerifySignature | `0x03` | PubKey (32) + Signature (64) + Data |
| SignData | `0x04` | Data to sign |
| EncryptData | `0x05` | Key (32) + Plaintext |
| DecryptData | `0x06` | Key (32) + MAC (2) + Ciphertext |
| KeyExchange | `0x07` | Remote PubKey (32) |
| Hash | `0x08` | Data to hash |
| SetRadio | `0x09` | Freq (4) + BW (4) + SF (1) + CR (1) |
| SetTxPower | `0x0A` | Chip power dBm (1) |
| GetRadio | `0x0B` | - |
| GetTxPower | `0x0C` | - |
| GetCurrentRssi | `0x0D` | - |
| IsChannelBusy | `0x0E` | - |
| GetAirtime | `0x0F` | Packet length (1) |
| GetNoiseFloor | `0x10` | - |
| GetVersion | `0x11` | - |
| GetStats | `0x12` | - |
| GetBattery | `0x13` | - |
| GetMCUTemp | `0x14` | - |
| GetSensors | `0x15` | Permissions (1) |
| GetDeviceName | `0x16` | - |
| Ping | `0x17` | - |
| Reboot | `0x18` | - |
| SetSignalReport | `0x19` | Enable (1): 0x00=disable, nonzero=enable |
| GetSignalReport | `0x1A` | - |
| GetOverrideStatus | `0x40` | - |
| SetOverride | `0x41` | Override TLV |
| ClearOverride | `0x42` | Override TLV or kind byte |
| ClearVolatileOverrides | `0x43` | - |
| GetEffectivePolicy | `0x45` | - |
| GetCapabilityStatus | `0x46` | - |
| ReportAdmissionFeedback | `0x47` | Outcome (1), optional MessageId (8) |
| ResetAdmissionState | `0x48` | - |
| SetAdmissionConfig | `0x49` | AdmissionConfigV1 (21) or AdmissionConfigV2 (33) |
| GetAdmissionConfig | `0x4A` | - |

### Response Sub-commands (TNC to Host)

Response codes use the high-bit convention: `response = command | 0x80`. Generic and unsolicited responses use the `0xF0`+ range.

| Sub-command | Value | Data |
|-------------|-------|------|
| Identity | `0x81` | PubKey (32) |
| Random | `0x82` | Random bytes (1-64) |
| Verify | `0x83` | Result (1): 0x00=invalid, 0x01=valid |
| Signature | `0x84` | Signature (64) |
| Encrypted | `0x85` | MAC (2) + Ciphertext |
| Decrypted | `0x86` | Plaintext |
| SharedSecret | `0x87` | Shared secret (32) |
| Hash | `0x88` | SHA-256 hash (32) |
| Radio | `0x8B` | Freq (4) + BW (4) + SF (1) + CR (1) |
| TxPower | `0x8C` | Power dBm (1) |
| CurrentRssi | `0x8D` | RSSI dBm (1, signed) |
| ChannelBusy | `0x8E` | Result (1): 0x00=clear, 0x01=busy |
| Airtime | `0x8F` | Milliseconds (4) |
| NoiseFloor | `0x90` | dBm (2, signed) |
| Version | `0x91` | Version (1) + Reserved (1) |
| Stats | `0x92` | RX (4) + TX (4) + Errors (4), optional QueueLen (2) + QueueCapacity (2) |
| Battery | `0x93` | Millivolts (2) |
| MCUTemp | `0x94` | Temperature (2, signed) |
| Sensors | `0x95` | CayenneLPP payload |
| DeviceName | `0x96` | Name (variable, UTF-8) |
| Pong | `0x97` | - |
| SignalReport | `0x9A` | Status (1): 0x00=disabled, 0x01=enabled |
| OverrideStatus | `0xA0` | Version (1) + Flags (1) + ActiveCount (1) + EffectiveRole (1) + LastReason (1) + NextTtlMs (4) |
| EffectivePolicy | `0xA2` | Same payload as OverrideStatus |
| CapabilityStatus | `0xA3` | Version (1) + NativeProtocolVersion (1) + BearerProfile (1) + EffectiveRole (1) + FeatureFlags (4) + SupportedTransports (2) + MaxLowRatePayloadBytes (2) + MaxQueueFrames (2) + OverrideFlags (1) + Reserved (1) |
| AdmissionConfigSet | `0xC9` | AdmissionConfigV2 (33) |
| AdmissionConfig | `0xCA` | AdmissionConfigV2 (33) |
| OK | `0xF0` | - |
| Error | `0xF1` | Error code (1) |
| TxDone | `0xF8` | Result (1), optional DurationMs (4) + QueueLen (2) + QueueCapacity (2) + scheduler diagnostics |
| RxMeta | `0xF9` | SNR (1) + RSSI (1) |

### Error Codes

| Code | Value | Description |
|------|-------|-------------|
| InvalidLength | `0x01` | Request data too short |
| InvalidParam | `0x02` | Invalid parameter value |
| NoCallback | `0x03` | Feature not available |
| MacFailed | `0x04` | MAC verification failed |
| UnknownCmd | `0x05` | Unknown sub-command |
| EncryptFailed | `0x06` | Encryption failed |
| TxInhibited | `0x07` | Local TX inhibit override rejected the send |
| TxQueueFull | `0x08` | TX queue or queued-airtime budget is full |
| TxBackpressure | `0x09` | Low-priority/data TX was rejected by scheduler high-water admission before queue-full |
| Busy | `0x0A` | Command cannot safely run while TX state or queue is active |

### Unsolicited Events

The TNC sends these SetHardware frames without a preceding request:

**TxDone (0xF8)**: Sent after a packet has been transmitted. Legacy payloads contain a single byte: 0x01 for success, 0x00 for failure. Extended payloads append TX duration, queue occupancy, point-in-time defer state, and optional scheduler drop counters.

**RxMeta (0xF9)**: Sent immediately after each standard data frame (type 0x00) with metadata for the received packet. Contains SNR (1 byte, signed, value x4 for 0.25 dB precision) followed by RSSI (1 byte, signed, dBm). Enabled by default; can be toggled with SetSignalReport. Standard KISS clients ignore this frame.

## Data Formats

### Radio Parameters (SetRadio / Radio response)

All values little-endian.

| Field | Size | Description |
|-------|------|-------------|
| Frequency | 4 bytes | Hz (e.g., 869618000) |
| Bandwidth | 4 bytes | Hz (e.g., 62500) |
| SF | 1 byte | Spreading factor (5-12) |
| CR | 1 byte | Coding rate (5-8) |

### Version (Version response)

| Field | Size | Description |
|-------|------|-------------|
| Version | 1 byte | Firmware version |
| Reserved | 1 byte | Always 0 |

Version `3` adds extended `Stats` and `TxDone` telemetry while retaining compatibility with legacy host parsers. Version `4` adds Cinder `CapabilityStatus` so hosts can gate protocol features on firmware-declared support instead of static board assumptions. Versions `5` through `7` add the Cinder bench priority queue, scheduler guard/defer diagnostics, and queued-airtime/drop telemetry. Version `8` adds low-priority/data backpressure before the four-frame queue reaches full scale. Version `9` keeps scheduler defer and drop reasons separated in `TxDone` telemetry. Version `10` tightens data backpressure when ACK, route, advert, or capability control traffic is already queued so low-rate data cannot consume control headroom during overload. Version `11` (`0B00`) adds randomized data admission/backoff and admission counters. Version `12` (`0C00`) fixes startup TX-power reporting. Version `13` (`0D00`) adds adaptive local data-admission congestion scoring and optional stats fields for the current admission windows. Version `14` (`0E00`) adds host-reported admission feedback for delivered, ACKed, lost, and ACK-timeout outcomes. Version `15` (`0F00`) adds deterministic admission-state reset for repeatable stress runs. Version `16` (`1000`) tunes board-local data admission with an airtime-floor release window, timed score decay, and gentler congestion scoring. Version `17` (`1100`) widens the base data-admission contention window to `1303-8000 ms` for the current max-payload profile. Version `18` (`1200`) adds runtime admission configuration. Version `19` (`1300`) adds observed-RX data guard telemetry and admission defer reason `observed-rx`. Version `20` (`1400`) makes the observed-RX guard runtime tunable through AdmissionConfig v2. Version `21` (`1500`) changes observed RX from a hard post-RX delay into a randomized admission-window bias. Version `22` (`1600`) changes observed RX into randomized queued-data retreat and appends observed-RX retreat counters to `Stats`. Version `23` (`1700`) parses native traffic classes for priority/admission decisions and advertises traffic-class admission support in `CapabilityStatus`. Version `24` (`1800`) keeps the same feature flags and payload formats but adds class-specific data-admission and busy-retreat windows. Version `25` (`1900`) maps host feedback message IDs to recently transmitted traffic classes and applies loss/ACK-timeout pressure per class. Version `26` (`1A00`) adds ACK-turn protected feedback pressure. Version `27` (`1B00`) makes congestion scoring class-specific. Version `28` (`1C00`) adds destination/neighbor-aware ACK-turn protection for native data. Version `29` (`1D00`) adds ACK/NACK channel-guard bypass and neighbor-busy telemetry. Version `30` (`1E00`) appends RadioLib receive-error diagnostics to `Stats`. Version `31` (`1F00`) separates first-send admission from retry pressure without changing `Stats`. Version `32` (`2000`) appends receive-error category counters to `Stats`. Version `33` (`2100`) keeps the payload format stable and turns categorized receive errors plus lost/ACK-timeout feedback into bounded channel-pressure admission input. Version `34` (`2200`) keeps the same receive-error pressure path but removes loss/ACK-timeout first-send pressure. Version `35` (`2300`) keeps the `Stats` payload stable and retimes queued/new data behind a randomized shared-channel cooldown after CRC/header receive-error deltas.

### CapabilityStatus (CapabilityStatus response)

All multi-byte values are little-endian. This is a firmware control-plane status response, not an RF-native Cinder frame. Hosts can use it to decide which native status or feature gates they should advertise on behalf of the radio.

| Field | Size | Description |
|-------|------|-------------|
| Version | 1 byte | Capability payload version, currently `1` |
| NativeProtocolVersion | 1 byte | Cinder native protocol version supported by this firmware, currently `1` |
| BearerProfile | 1 byte | `0x02` = KISS bench bearer |
| EffectiveRole | 1 byte | `0x00` auto, `0x01` leaf, `0x02` relay, `0x03` quiet |
| FeatureFlags | 4 bytes | Cinder capability bitset; current firmware sets override control, firmware diagnostics, admission feedback, admission reset, admission config, and traffic-class admission |
| SupportedTransports | 2 bytes | Cinder transport bitmask; current firmware sets LoRa and serial |
| MaxLowRatePayloadBytes | 2 bytes | Current Cinder low-rate payload target, `192` bytes |
| MaxQueueFrames | 2 bytes | Firmware TX queue capacity, currently `4` |
| OverrideFlags | 1 byte | Same flag mask used by OverrideStatus |
| Reserved | 1 byte | Must be ignored by hosts |

### Encrypted (Encrypted response)

| Field | Size | Description |
|-------|------|-------------|
| MAC | 2 bytes | HMAC-SHA256 truncated to 2 bytes |
| Ciphertext | variable | AES-128 block-encrypted data with zero padding |

### Airtime (Airtime response)

All values little-endian.

| Field | Size | Description |
|-------|------|-------------|
| Airtime | 4 bytes | uint32_t, estimated air time in milliseconds |

### Noise Floor (NoiseFloor response)

All values little-endian.

| Field | Size | Description |
|-------|------|-------------|
| Noise floor | 2 bytes | int16_t, dBm (signed) |

The modem recalibrates the noise floor every 2 seconds with an AGC reset every 30 seconds.

### Stats (Stats response)

All values little-endian.

| Field | Size | Description |
|-------|------|-------------|
| RX | 4 bytes | Packets received |
| TX | 4 bytes | Packets transmitted |
| Errors | 4 bytes | Receive errors |
| QueueLen | 2 bytes | Optional pending TX queue depth |
| QueueCapacity | 2 bytes | Optional TX queue capacity |
| SchedulerDelayMs | 4 bytes | Optional next scheduler/admission delay |
| SchedulerDeferReason | 1 byte | Optional current scheduler defer reason |
| TxState | 1 byte | Optional scheduler TX state |
| SchedulerQueuedAirtimeMs | 4 bytes | Optional currently queued firmware-estimated airtime |
| SchedulerAirtimeBudgetMs | 4 bytes | Optional queued-airtime budget |
| SchedulerDropCount | 4 bytes | Optional cumulative scheduler rejects/drops |
| SchedulerLastDropReason | 1 byte | Optional last scheduler drop reason |
| AdmissionAcceptCount | 4 bytes | Optional cumulative firmware admission accepts |
| AdmissionDeferCount | 4 bytes | Optional cumulative randomized or busy-channel admission deferrals |
| AdmissionBackoffCount | 4 bytes | Optional cumulative randomized data backoff decisions |
| AdmissionBusyCount | 4 bytes | Optional cumulative busy-channel data retreat decisions |
| AdmissionRejectCount | 4 bytes | Optional cumulative admission rejects/backpressure decisions |
| LastAdmissionDelayMs | 4 bytes | Optional last randomized admission delay |
| LastAdmissionReason | 1 byte | Optional last admission reason |
| AdmissionCongestionScore | 1 byte | Optional local data-admission congestion score, `0` through `8` |
| AdmissionWindowMinMs | 4 bytes | Optional current randomized data-admission window minimum for a max-payload bench frame |
| AdmissionWindowMaxMs | 4 bytes | Optional current randomized data-admission window maximum for a max-payload bench frame |
| AdmissionBusyWindowMaxMs | 4 bytes | Optional current busy-channel retreat window maximum for a max-payload bench frame |
| AdmissionFeedbackSuccessCount | 4 bytes | Optional cumulative delivered/ACKed host feedback applied to admission scoring |
| AdmissionFeedbackFailureCount | 4 bytes | Optional cumulative lost/ACK-timeout host feedback applied to admission scoring |
| LastAdmissionFeedback | 1 byte | Optional last host feedback outcome |
| ObservedRxCount | 4 bytes | Optional cumulative packets that updated the observed-RX guard |
| ObservedRxGuardDelayMs | 4 bytes | Optional current remaining data-only observed-RX guard delay |
| LastObservedRxAirtimeMs | 4 bytes | Optional firmware-estimated airtime of the last observed RX packet |
| ObservedRxRetreatCount | 4 bytes | Optional cumulative data-admission retreats caused by an active observed-RX window |
| LastObservedRxRetreatMs | 4 bytes | Optional last randomized observed-RX retreat applied to new or queued data |
| NeighborBusyObservedCount | 4 bytes | Optional cumulative native data packets that updated neighbor-busy memory |
| NeighborBusyDeferCount | 4 bytes | Optional cumulative scheduler defer episodes caused by matching neighbor-busy memory |
| LastNeighborBusyDelayMs | 4 bytes | Optional last matching neighbor-busy delay reported by the scheduler |
| AckGuardBypassCount | 4 bytes | Optional cumulative queued ACK/NACK episodes that bypassed the post-TX channel guard |
| LastRecvErrorCode | 2 bytes | Optional signed RadioLib error code from the last failed `readData` |
| LastRecvErrorLen | 2 bytes | Optional packet length reported before the last failed `readData` |
| LastStartRecvErrorCode | 2 bytes | Optional signed RadioLib error code from the last failed `startReceive` |
| StartRecvErrorCount | 4 bytes | Optional cumulative `startReceive` failures |
| RecvCrcErrorCount | 4 bytes | Optional cumulative failed `readData` count where RadioLib returned CRC mismatch (`-7`) |
| RecvHeaderErrorCount | 4 bytes | Optional cumulative failed `readData` count where RadioLib returned damaged LoRa header (`-24`) |
| RecvOtherErrorCount | 4 bytes | Optional cumulative failed `readData` count for any other RadioLib error |

The current Cinder bench KISS modem uses a four-frame priority TX queue. Hosts should continue to accept the legacy 12-byte payload without queue fields.

### ReportAdmissionFeedback (command `0x47`)

Firmware `0E00` accepts host-reported delivery outcomes so a controller that understands Cinder message IDs can feed end-to-end observations back into board-local admission. The firmware only uses the outcome byte today; the optional message ID is accepted for host/protocol alignment and future debugging.

| Field | Size | Description |
|-------|------|-------------|
| Outcome | 1 byte | `0x01` delivered, `0x02` ACKed, `0x03` lost, `0x04` ACK timeout |
| MessageId | 8 bytes | Optional little-endian Cinder message ID |

### ResetAdmissionState (command `0x48`)

Firmware `0F00` clears benchmark-facing admission state while preserving RX/TX/error counters and queued traffic. The command resets scheduler drop counters/reasons, admission counters, host feedback counters, local congestion score, last admission reason/delay, and pending post-TX admission guard. It returns `Busy` if the TX state is not idle or the TX queue is not empty.

| Field | Size | Description |
|-------|------|-------------|
| - | - | Empty payload |

### AdmissionConfig (commands `0x49` / `0x4A`)

Firmware `1200` accepts a fixed versioned payload for runtime admission tuning. Firmware `1400` extends that payload to v2 with observed-RX guard controls while still accepting v1 set requests. Firmware `1500` preserves the v2 payload but uses the observed-RX values as an admission-window bias instead of a hard scheduler guard. Firmware `1600` keeps v2 and uses the same observed-RX values as a randomized retreat window for data admission. Firmware `1700` keeps the same config schema; traffic-class priority is fixed by native frame class and does not add a config field yet. Firmware `1800` still keeps the same config schema; `DataAdmissionMaxMs` remains the configured global cap, while class-specific estimated-airtime caps shape the zero-score window below that cap. Firmware `1900+` also keeps the same config schema and folds class-specific feedback and congestion pressure into those effective windows after host feedback or busy/drop events. `SetAdmissionConfig` resets admission counters and score after applying a valid idle-time config. It returns `Busy` if the TX path is not idle. `GetAdmissionConfig` returns the active config. Hard firmware caps still apply: data max `8000 ms`, busy max `6000 ms`, decay interval `1000..600000 ms`, observed-RX guard max `6000 ms`, and observed-RX guard percent `0..200`.

| Field | Size | Description |
|-------|------|-------------|
| Version | 1 byte | Admission config payload version: `1` for the original 21-byte payload, `2` for the 33-byte observed-RX guard payload |
| DataAdmissionMinMs | 4 bytes | Base randomized data-admission window minimum |
| DataAdmissionMaxMs | 4 bytes | Base randomized data-admission window maximum |
| BusyBackoffMinMs | 4 bytes | Busy-channel retreat window minimum |
| BusyBackoffMaxMs | 4 bytes | Busy-channel retreat window maximum |
| CongestionDecayIntervalMs | 4 bytes | Time between local congestion-score decay steps |
| ObservedRxGuardMinMs | 4 bytes | v2 only: minimum observed-RX data admission window; firmware `1400` used this as hard quiet time and firmware `1500` used it as bias |
| ObservedRxGuardMaxMs | 4 bytes | v2 only: maximum observed-RX data admission window; use `0` with percent `0` to disable observed-RX admission effects |
| ObservedRxGuardPercent | 4 bytes | v2 only: percent of estimated observed packet airtime to use before min/max clamping; `0` disables observed-RX admission effects |

### TxDone (TxDone event)

All multi-byte values are little-endian.

| Field | Size | Description |
|-------|------|-------------|
| Result | 1 byte | `0x00` = failed, `0x01` = success |
| DurationMs | 4 bytes | Optional elapsed radio-send time in milliseconds |
| QueueLen | 2 bytes | Optional pending TX queue depth after completion |
| QueueCapacity | 2 bytes | Optional TX queue capacity |
| SchedulerDelayMs | 4 bytes | Optional next scheduler/admission delay |
| SchedulerDeferReason | 1 byte | Optional current post-TX scheduler defer reason |
| SchedulerDropCount | 4 bytes | Optional cumulative scheduler rejects/drops |
| SchedulerLastDropReason | 1 byte | Optional last scheduler drop reason |

Hosts should continue to accept the legacy one-byte payload. Firmware `0900` and later append drop counters after the defer fields so a successful TX no longer inherits an unrelated `backpressure` or `queue-full` reason from a later rejected frame.

Firmware `0C00` makes `GetTxPower` report the board's configured startup chip power before any host-side `SetTxPower` command. Earlier experimental builds only reported the last KISS-set value and could return `0` immediately after boot even when the radio had initialized at the board default.

Firmware `0D00` appends the adaptive data-admission congestion score and current backoff windows to `Stats`. Legacy hosts can ignore these trailing bytes.

Firmware `0E00` appends host feedback counters to `Stats`. Legacy hosts can ignore these trailing bytes.

Firmware `0F00` does not change the `Stats` payload format; it adds `ResetAdmissionState` so these counters and score fields can be zeroed before controlled runs.

Firmware `1000` also leaves the `Stats` payload format unchanged. Hosts should interpret changed score/window trends as policy behavior, not as a new telemetry schema.

Firmware `1100` also leaves the `Stats` payload format unchanged. The expected zero-score max-payload data window is `1303-8000 ms` with the current modem profile.

Firmware `1200` also leaves the `Stats` payload format unchanged. Hosts should use `GetAdmissionConfig` to record the raw configured policy and `Stats` to record the effective score-weighted windows.

Firmware `1400` keeps the `Stats` payload format from `1300` and extends `AdmissionConfig` to v2. Hosts can use `ObservedRxGuardPercent=0` plus `ObservedRxGuardMinMs=0` and `ObservedRxGuardMaxMs=0` to disable the post-RX guard for A/B testing without reflashing.

Firmware `1500` keeps the same `Stats` and `AdmissionConfig` payloads. The `ObservedRxGuardDelayMs` stats field reports the active observed-RX bias window, but that value is no longer a direct scheduler delay; it is folded into the randomized data-admission floor for newly queued data frames.

Firmware `1600` appends `ObservedRxRetreatCount` and `LastObservedRxRetreatMs` to `Stats`. `ObservedRxGuardDelayMs` still reports the active observed-RX window, but the primary scheduler action is randomized retreat for low-priority data either at enqueue time or when already queued data overhears another data packet.

Firmware `1700` leaves the `Stats` payload unchanged. Hosts should validate traffic-class behavior by comparing frame class in native telemetry, capability flags, queue/admission counters, and stress outcomes.

Firmware `1800` also leaves the `Stats` payload unchanged. The reported admission window remains the interactive data window for the reference packet size; class-specific telemetry/bulk windows are intentionally inferred from native frame class plus stress outcomes until the stats schema grows per-class admission counters.

Firmware `1900` leaves the `Stats` payload unchanged. Feedback success/failure counters remain global, while message-ID keyed pressure is applied internally per traffic class.

Firmware `1A00` leaves the `Stats` payload unchanged. It bounds per-class feedback pressure more tightly, treats ACK-timeout feedback as one pressure step instead of two, and adds a 600 ms data-only ACK-turn protect window after hearing data so queued data does not immediately contend with expected ACK/control traffic. Hosts may see this protect window as an `observed-rx` scheduler defer reason, but no host parsing change is required.

Firmware `1B00` also leaves the `Stats` payload unchanged. Congestion score is now maintained internally per traffic class; the legacy `DataCongestionScore` stats byte reports the interactive reference score so existing host summaries keep parsing the frame.

Firmware `1C00` leaves the `Stats` payload unchanged. Native Cinder data with a parsed destination handle uses neighbor-busy memory instead of broad ACK-turn delay; matching delays still report through the existing `observed-rx` scheduler defer reason. Non-native or malformed data keeps the broad post-RX ACK-turn fallback, so hosts do not need parser changes for mixed protocol tests.

Firmware `1D00` appends `NeighborBusyObservedCount`, `NeighborBusyDeferCount`, `LastNeighborBusyDelayMs`, and `AckGuardBypassCount` to `Stats`. The scheduler now reports matching native neighbor-busy delay as defer reason `neighbor-busy` (`0x0A`) instead of overloading `observed-rx`. ACK/NACK frames still keep priority `0` and now bypass the post-TX channel guard so delayed data backlog does not hold delivery feedback behind the normal data quiet period.

Firmware `1E00` appends `LastRecvErrorCode`, `LastRecvErrorLen`, `LastStartRecvErrorCode`, and `StartRecvErrorCount` to `Stats`. `Errors` remains the cumulative failed `readData` count for compatibility. The new fields distinguish corrupt/CRC/header receive failures from radio state-machine failures where `startReceive` itself returns an error.

Firmware `2000` appends `RecvCrcErrorCount`, `RecvHeaderErrorCount`, and `RecvOtherErrorCount` to `Stats`. `Errors` remains the compatibility aggregate of all failed `readData` calls; hosts should use the category counters for RF trend analysis and keep `StartRecvErrorCount` separate as a radio state-machine diagnostic.

Firmware `2100` keeps the `Stats` payload unchanged. The modem samples the FW2000 receive-error category counters every 250 ms; new CRC or damaged-header deltas raise all data-class congestion scores, while `other` read failures raise a smaller channel-pressure step. Lost and ACK-timeout host feedback also raise the affected traffic class congestion score by one step, so first-send admission can react to real contention without changing host parsing.

Firmware `2200` keeps the `Stats` payload unchanged and preserves the 250 ms receive-error sampling path, but removes the lost/ACK-timeout first-send congestion bump from `2100`. Lost feedback still updates retry pressure, while only categorized receive errors can widen first-send admission as channel-pressure evidence.

Firmware `2300` keeps the `Stats` payload unchanged. When the 250 ms sampler observes new CRC mismatch or damaged-header deltas, the modem raises the data-class congestion score and starts a short shared-channel cooldown derived from the current LoRa airtime estimate. New data frames draw extra randomized retreat from that cooldown, and any currently queued data frame can be retimed behind it. ACK/control traffic still bypasses the data gate.

### Cinder Gateway Artifact Publish

The Cinder gateway GUI firmware updater accepts ESP32-S3 app images built by PlatformIO. Publish a build artifact from this checkout with:

```bash
python3 scripts/publish_cinder_firmware.py \
  --gateway http://10.0.0.68:9080 \
  --env Heltec_v3_kiss_modem \
  --env heltec_v4_kiss_modem \
  --env LilyGo_T-Echo_kiss_modem \
  --build
```

The script reads `KISS_FIRMWARE_VERSION`, converts it to the existing display version format such as `0A00`, uploads the correct PlatformIO artifact for each target, and records the current git commit in the gateway artifact manifest. Heltec V3/V4 artifacts use `firmware.bin` and the gateway writes only the ESP32 app image at offset `0x10000`; T-Echo artifacts use the nRF52 `firmware.zip` DFU package.

### Battery (Battery response)

All values little-endian.

| Field | Size | Description |
|-------|------|-------------|
| Millivolts | 2 bytes | uint16_t, battery voltage in mV |

### MCU Temperature (MCUTemp response)

All values little-endian.

| Field | Size | Description |
|-------|------|-------------|
| Temperature | 2 bytes | int16_t, tenths of °C (e.g., 253 = 25.3°C) |

Returns `NoCallback` error if the board does not support temperature readings.

### Device Name (DeviceName response)

| Field | Size | Description |
|-------|------|-------------|
| Name | variable | UTF-8 string, no null terminator |

### Reboot

Sends an `OK` response, flushes serial, then reboots the device. The host should expect the connection to drop.

### Sensor Permissions (GetSensors)

| Bit | Value | Description |
|-----|-------|-------------|
| 0 | `0x01` | Base (battery) |
| 1 | `0x02` | Location (GPS) |
| 2 | `0x04` | Environment (temp, humidity, pressure) |

Use `0x07` for all permissions.

### Sensor Data (Sensors response)

Data returned in CayenneLPP format. See [CayenneLPP documentation](https://docs.mydevices.com/docs/lorawan/cayenne-lpp) for parsing.

## Cryptographic Algorithms

| Operation | Algorithm |
|-----------|-----------|
| Identity / Signing / Verification | Ed25519 |
| Key Exchange | X25519 (ECDH) |
| Encryption | AES-128 block encryption with zero padding + HMAC-SHA256 (MAC truncated to 2 bytes) |
| Hashing | SHA-256 |

## Notes

- Data payload limit (255 bytes) matches MeshCore MAX_TRANS_UNIT; no change needed for KISS “1024+ recommended” (that applies to general TNCs, not MeshCore)
- Modem generates identity on first boot (stored in flash)
- All multi-byte values are little-endian unless stated otherwise
- SNR values in RxMeta are multiplied by 4 for 0.25 dB precision
- TxDone is sent as a SetHardware event after each transmission
- Standard KISS clients receive only type 0x00 data frames and can safely ignore all SetHardware (0x06) frames
- See [packet_format.md](./packet_format.md) for packet format
