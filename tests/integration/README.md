chmod +x tests/integration/setup_netns.sh tests/integration/run_netns_tests.sh
make
sudo tests/integration/run_netns_tests.sh# EtherIP Integration Tests (netns)

Purpose
- Start two `etherip` instances in separate network namespaces and verify MTU/ping and optional iperf3 throughput.

Prerequisites
- Linux with `iproute2` and network namespaces support
- `sudo` (tests require CAP_NET_ADMIN and CAP_NET_RAW)
- `iperf3` and `tcpdump` installed (optional but recommended)
- Build `etherip` in repository root (`make`)
- In devcontainer, rebuild after adding `--device=/dev/net/tun` and the required caps in `.devcontainer/devcontainer.json`

Files
- `tests/integration/setup_netns.sh` — create/destroy namespaces, veth pairs, and TAPs
- `tests/integration/run_netns_tests.sh` — start two `etherip` instances, run `ping` with DF, optional `iperf3`, then cleanup

Quick start
```bash
chmod +x tests/integration/setup_netns.sh tests/integration/run_netns_tests.sh
make            # build etherip if needed
# Run tests (requires root)
sudo tests/integration/run_netns_tests.sh
```

Notes
- The ping test uses `ping -M do -s 1472` to send an inner IP packet of 1500 bytes (20B IP + 8B ICMP + 1472 payload = 1500). DF is set on the inner packet; fragmentation of the outer (encapsulated) packet is allowed.
- To point to a custom `etherip` binary path, set `ETHERIP_BIN` before running:
```bash
export ETHERIP_BIN=/path/to/etherip
sudo tests/integration/run_netns_tests.sh
```
- Logs are stored under `/tmp/etherip-test-<pid>` and the script prints the path on completion.

CI
- These tests require privileged operations and are not suitable for unprivileged CI runners. Run them on a privileged runner or dedicated test host.
