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

Only a small input-stack change to `himawari-rx` was expected; the assembler / decoder logic
was not the target of this experiment.

## Files

- `socks.c` — FIFO reader and TCP sender.
- `TCP_Recv.py` — minimal TCP receiver used to verify that data forwarding worked.

## Status

**Proof of concept / historical experiment.**

The FIFO -> TCP forwarding code was tested and data transfer worked.

However, the project was not completed to the point of:

- integrating the TCP receiver into `himawari-rx`;
- integrating it into
  [`tcjj3/himawari-rx_auto_scripts`](https://github.com/tcjj3/himawari-rx_auto_scripts);
- running an end-to-end test against a live HimawariCast satellite receive chain.

By the time this experiment was written, the original satellite receiving environment was no
longer available for field testing.

The source files in this snapshot date from **2024-06-05**.

## Build

Linux / POSIX environment:

```bash
gcc -Wall -Wextra -O2 -o file2tcp socks.c
```

## Basic usage

Start the test receiver:

```bash
python3 TCP_Recv.py
```

Compile and start the sender with an explicit FIFO path:

```bash
./file2tcp 127.0.0.1 9999 /tmp/mysocket
```

Then point the producer at the FIFO.

The original prototype source is published here largely as an engineering record, so readers
can inspect the actual mechanism rather than only a retrospective description.

## Known prototype limitations

This is the original experimental code, not a production-hardened release.

Notable limitations include:

- the current `socks.c` argument handling effectively expects an explicit FIFO path;
- `send()` partial-write handling is not implemented;
- reconnect / retry loops do not use backoff;
- `TCP_Recv.py` is only a transport test receiver and prints received bytes as UTF-8 rather
  than implementing the binary `himawari-rx` input path;
- no end-to-end live satellite test was completed.

## Related work

- [`sam210723/himawari-rx`](https://github.com/sam210723/himawari-rx)
- [`sam210723/himawari-rx#2`](https://github.com/sam210723/himawari-rx/issues/2)
- [`tcjj3/himawari-rx_auto_scripts`](https://github.com/tcjj3/himawari-rx_auto_scripts)

## License

MIT License. See [LICENSE](LICENSE).
