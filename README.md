# File2TCP

Experimental FIFO-to-TCP transport bridge for packet-sensitive satellite receiving pipelines.

## Why this exists

This project was designed as a follow-up to a reliability problem found while testing
[`sam210723/himawari-rx`](https://github.com/sam210723/himawari-rx).

In the original HimawariCast receive chain:

```text
TSDuck -> UDP -> himawari-rx
```

the decoder intermittently showed symptoms such as `PART BEFORE INFO` and `MISSING PARTS`.
When the same receive path was changed to:

```text
TSDuck -> packet dump file -> himawari-rx --file
```

the data could be processed normally.

The investigation is documented publicly in
[`sam210723/himawari-rx#2`](https://github.com/sam210723/himawari-rx/issues/2).

The engineering conclusion for this receive path was simple: the decoder needed an
**order-preserving input path** rather than an inter-component UDP handoff. It was not
necessary to distinguish whether packet loss, packet reordering, or both were the immediate
cause; the UDP path was the failing transport boundary while file input was the working one.

A separate GK-2A/GNU Radio experiment in 2021 had shown a similar transport-level pattern:
native SDR access and an `RSP_TCP` path worked, while the tested UDP-coupled path did not.
That result provided an earlier practical precedent for using a TCP stream boundary in a
satellite receiving chain.

## Intended architecture

File2TCP was a later attempt to keep the ordering properties of the working file-input path
while making the transport streamable:

```text
TSDuck
   |
   v
named FIFO
   |
   v
socks.c
   |
   v
TCP byte stream
   |
   v
modified himawari-rx input stack
```

The intended integration was:

1. TSDuck writes its packet-dump output to a named FIFO.
2. `socks.c` reads the FIFO sequentially.
3. The bytes are forwarded over a TCP connection.
4. `himawari-rx` is modified at its input layer to consume the TCP byte stream using the
   same packet-length framing logic already used by its `--file` mode.

The assembler / decoder logic was not the target of the experiment. The important change is
at the input boundary.

## Code history

The original source was written in 2024 and was not published at the time because live
HimawariCast field testing was no longer available.

The timestamps preserved in the original ZIP archive are:

- `socks.c` — **2024-06-05 16:39**
- `TCP_Recv.py` — **2024-06-05 17:13**

ZIP timestamps do not encode a timezone here, so those values are preserved as archive-local
timestamps rather than being reinterpreted.

The unmodified 2024 prototype was first committed to this repository when it was publicly
released. Its exact source remains available in Git history.

A public hardening pass was made on **2026-10-08**. That pass does not claim new live-satellite
validation; it improves the transport utility itself while preserving the original design.

### 2026 hardening changes

`socks.c` now includes:

- correct optional-argument handling and TCP port validation;
- safe FIFO creation without blindly unlinking an unrelated existing file;
- `send()` partial-write handling;
- `EINTR` handling;
- `SIGPIPE` protection;
- connection-establishment retry with exponential backoff instead of a busy loop;
- clean handling when a FIFO writer closes and a later writer reconnects;
- hostname / IPv4 / IPv6 resolution via `getaddrinfo()`;
- explicit fail-fast behavior on a mid-stream TCP disconnect instead of pretending that a
  new TCP connection can resume at a provably identical byte offset.

`TCP_Recv.py` was also made binary-safe and can optionally append received bytes to a file for
transport verification.

The hardened sender was compiled with GCC using `-Wall -Wextra -Wpedantic`, and a local
FIFO -> TCP binary-payload test verified byte-for-byte transfer through the test receiver.

## Files

- `socks.c` — FIFO reader and TCP sender.
- `TCP_Recv.py` — binary-safe TCP receiver used to verify transport behavior.

## Build

Linux / POSIX environment:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -o file2tcp socks.c
```

## Basic usage

Start the test receiver:

```bash
python3 TCP_Recv.py
```

Optionally save the received binary stream:

```bash
python3 TCP_Recv.py --output received.dump
```

Start the sender:

```bash
./file2tcp 127.0.0.1 9999 /tmp/mysocket
```

The FIFO path is optional; the default is `/tmp/mysocket`:

```bash
./file2tcp 127.0.0.1 9999
```

Then configure the producer to write its packet-dump output to that FIFO.

## Why mid-stream reconnect is deliberately not hidden

TCP guarantees an ordered byte stream **within one connection**, but after a connection is
lost the sender cannot prove exactly how many previously accepted bytes reached the remote
application before the failure.

Silently opening a new TCP connection and continuing from an assumed offset can therefore
create an undetectable gap or duplication in a packet-sensitive stream.

The hardened sender retries the initial TCP connection with backoff, but if an established
stream breaks it exits with an explicit error. For a simple satellite receive pipeline, the
safe recovery is to restart the affected producer / bridge / decoder chain. Seamless recovery
would require an application-layer protocol with framing plus sequence / acknowledgement
state.

## Suggested TCP input modification for himawari-rx

The original `himawari-rx` already contains almost all of the framing logic needed for TCP.
Its `--file` input path currently does this:

1. read a 6-byte packet header;
2. decode the packet length from `header[2:4]` as little-endian;
3. read the remaining `length - 6` bytes;
4. push the complete packet into the existing `Assembler`.

A TCP mode can reuse exactly that framing model. The decoder / assembler should not need to
be rewritten.

### Recommended CLI shape

For example, add options such as:

```python
argp.add_argument(
    "--tcp-listen",
    type=int,
    default=None,
    help="Listen for File2TCP input on this TCP port"
)
argp.add_argument(
    "--tcp-bind",
    default="0.0.0.0",
    help="Address for --tcp-listen (default: 0.0.0.0)"
)
```

`--file` and `--tcp-listen` should be treated as mutually exclusive input modes. The existing
UDP mode can remain the default for compatibility.

### Configure a TCP listener

A minimal branch in `config_input()` could look like:

```python
elif self.args.tcp_listen is not None:
    self.sck = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    self.sck.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    self.sck.bind((self.args.tcp_bind, self.args.tcp_listen))
    self.sck.listen(1)
    self.tcp_client = None
```

### Do not treat one recv() call as one packet

TCP has no datagram boundaries. One `recv()` may return half a Himawari packet, one exact
packet, or several packet fragments concatenated together.

Use an exact-length helper:

```python
def recv_exact(sock, size):
    data = bytearray()

    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("TCP stream closed")
        data.extend(chunk)

    return bytes(data)
```

Then the TCP receive branch can mirror the proven file parser:

```python
if self.tcp_client is None:
    self.tcp_client, addr = self.sck.accept()
    print(f"TCP INPUT: {addr[0]}:{addr[1]}")

try:
    header = recv_exact(self.tcp_client, 6)
    packet_length = int.from_bytes(header[2:4], "little")

    if packet_length < 6:
        raise ValueError(f"Invalid packet length: {packet_length}")

    packet = header + recv_exact(self.tcp_client, packet_length - 6)
    self.assembler.push(packet)

except (ConnectionError, OSError, ValueError) as exc:
    print(f"TCP INPUT CLOSED: {exc}")
    self.tcp_client.close()
    self.tcp_client = None
```

An alternative minimal implementation is to wrap the accepted socket with
`socket.makefile("rb")` and reuse the existing file-reading branch, but an explicit
`recv_exact()` makes the TCP framing requirements easier to see and audit.

### Important reconnect rule

A newly accepted TCP connection must begin at a valid packet boundary. If a TCP connection
breaks in the middle of a packet, simply reconnecting File2TCP at an arbitrary FIFO byte
offset is not enough to guarantee stream correctness.

For the first implementation, the simplest safe behavior is:

```text
TCP stream breaks
        -> stop / restart TSDuck output
        -> restart File2TCP
        -> accept a fresh himawari-rx TCP session
        -> begin again at a known packet boundary
```

If seamless session recovery is later required, add an application framing / sequence / ACK
layer rather than relying on TCP reconnect alone.

### Why this is a small input-stack change

The existing file mode already proves that `himawari-rx` can consume this ordered packet-dump
format correctly. A TCP listener only replaces the source of those bytes:

```text
existing:
packet file -> read(6) -> read(length - 6) -> Assembler.push(packet)

proposed:
TCP stream  -> recv_exact(6) -> recv_exact(length - 6) -> Assembler.push(packet)
```

That is the core reason File2TCP was designed as a transport bridge rather than as a decoder
rewrite.

## Status

**Proof of concept / historical experiment, now publicly documented and transport-hardened.**

The FIFO -> TCP forwarding mechanism works and has been locally re-verified after the 2026
hardening pass.

What remains unverified is the original final target:

- TCP input integrated directly into `himawari-rx`;
- integration with
  [`tcjj3/himawari-rx_auto_scripts`](https://github.com/tcjj3/himawari-rx_auto_scripts);
- end-to-end validation against a live HimawariCast satellite receive chain.

Those steps were not completed because the original receiving environment was no longer
available.

## Related work

- [`sam210723/himawari-rx`](https://github.com/sam210723/himawari-rx)
- [`sam210723/himawari-rx#2`](https://github.com/sam210723/himawari-rx/issues/2)
- [`tcjj3/himawari-rx_auto_scripts`](https://github.com/tcjj3/himawari-rx_auto_scripts)

## License

MIT License. See [LICENSE](LICENSE).
