# ITCH Streaming Architecture

The simulator produces a real-time ITCH 5.0 feed over UDP, framed with
MoldUDP64.  Two transport modes are supported: **unicast** (default in Docker)
and **multicast** (default on physical networks).

## Quick Start (Docker)

```bash
docker compose --profile platform up -d
docker compose --profile platform logs -f itch-listener
```

The `itch-listener` service decodes and prints every ITCH message it receives:

```
[seq=1] SYSTEM_EVENT code=O ts=0
[seq=2] STOCK_DIRECTORY stock=AAPL     locate=1 ts=0
[seq=3] ADD_ORDER ref=1 side=B shares=1 stock=AAPL     price=100.0000 ts=34200059888858
```

## Transport Modes

### Unicast (Docker default)

The `itch-stream` service sends UDP datagrams directly to a single destination
using the `--unicast-dest host:port` flag:

```
qrsdp_itch_stream --unicast-dest itch-listener:5001 ...
```

The listener receives these as plain UDP on the bound port with no multicast
group membership required (`--no-multicast`):

```
qrsdp_listen --port 5001 --no-multicast
```

**Why unicast in Docker?**  Docker bridge networks do not forward multicast
traffic between containers. Neither `docker0` nor user-defined bridge networks
support IGMP or multicast routing, so `IP_ADD_MEMBERSHIP` succeeds but packets
are silently dropped.  Unicast works on any Docker network driver (bridge,
overlay, macvlan).

**Limitation:** Only one listener receives the feed.  For fan-out, use
multicast on a physical network or add application-level replication.

### Multicast (physical / bare-metal networks)

When `--unicast-dest` is omitted, the ITCH stream sends to a multicast group
(default `239.1.1.1:5001`):

```
qrsdp_itch_stream --multicast-group 239.1.1.1 --port 5001 ...
```

Any number of listeners can join the group:

```
qrsdp_listen --multicast-group 239.1.1.1 --port 5001
```

**Network requirements:**

| Requirement | Detail |
|---|---|
| IGMP snooping | Must be enabled on L2 switches so multicast is forwarded only to interested ports. |
| Multicast routing (PIM) | Required only if listeners are on a different subnet than the sender. |
| TTL | Default TTL is 1 (local subnet). Increase with `--ttl` for cross-subnet delivery. |

### Cloud environments

| Provider | Multicast support | Workaround |
|---|---|---|
| **AWS** | Not on standard VPCs. Supported via Transit Gateway multicast domains. | Use unicast with `--unicast-dest`, or configure a Transit Gateway multicast domain. |
| **GCP** | Not supported on VPC networks. | Use unicast with `--unicast-dest`. |
| **Azure** | Not supported on standard VNets. | Use unicast with `--unicast-dest`. |

For cloud deployments where multicast is unavailable, use `--unicast-dest` with
a known listener endpoint, or implement application-level fan-out behind a load
balancer.

## CLI Reference

### qrsdp_itch_stream

| Flag | Default | Description |
|---|---|---|
| `--kafka-brokers` | `localhost:9092` | Kafka bootstrap servers |
| `--kafka-topic` | `exchange.events` | Kafka topic to consume |
| `--consumer-group` | `itch-streamer` | Kafka consumer group ID |
| `--multicast-group` | `239.1.1.1` | Multicast group (ignored if `--unicast-dest` set) |
| `--unicast-dest` | *(none)* | Send unicast to `host:port` instead of multicast |
| `--port` | `5001` | UDP port (multicast mode only) |
| `--tick-size` | `100` | Tick size in price-4 units |

### qrsdp_listen

| Flag | Default | Description |
|---|---|---|
| `--multicast-group` | `239.1.1.1` | Multicast group to join |
| `--port` | `5001` | UDP port to bind |
| `--no-multicast` | *(off)* | Skip `IP_ADD_MEMBERSHIP`; receive unicast only |

## Docker Compose Services

The `platform` profile provides the full streaming pipeline:

| Service | Binary | Role |
|---|---|---|
| `kafka` | Apache Kafka 3.7 | Message broker |
| `kafka-producer` | `qrsdp_run` | Generates events, publishes to Kafka |
| `itch-stream` | `qrsdp_itch_stream` | Consumes Kafka, encodes ITCH, sends UDP unicast |
| `itch-listener` | `qrsdp_listen` | Receives UDP, decodes and prints ITCH messages |

```
kafka-producer ──► Kafka ──► itch-stream ──(UDP unicast)──► itch-listener
```
