#!/bin/bash
set -euo pipefail
SCRIPTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ETHERIP_BIN="${ETHERIP_BIN:-$(pwd)/etherip}"
ETHERIP_MTU="${ETHERIP_MTU:-1536}"
LOGDIR="/tmp/etherip-test-$$"
mkdir -p "$LOGDIR"

# 1) setup
sudo "$SCRIPTDIR/setup_netns.sh" create

# 2) start etherip in each ns (background)
sudo ip netns exec ns1 bash -c "cd /workspaces/etherip || cd /; $ETHERIP_BIN --mtu $ETHERIP_MTU ipv4 dst 10.10.10.2 src 10.10.10.1 tap tap1 > $LOGDIR/etherip-ns1.log 2>&1 & echo \$! > $LOGDIR/etherip-ns1.pid"
sudo ip netns exec ns2 bash -c "cd /workspaces/etherip || cd /; $ETHERIP_BIN --mtu $ETHERIP_MTU ipv4 dst 10.10.10.1 src 10.10.10.2 tap tap2 > $LOGDIR/etherip-ns2.log 2>&1 & echo \$! > $LOGDIR/etherip-ns2.pid"

sleep 1

# MTU/DF test:
# etherip --mtu 1536 gives tap MTU 1500 on IPv4, so an ICMP payload of 1472
# creates a 1500-byte inner IP packet. DF is set on the inner packet.
echo "Running ping with DF (no-fragment) on inner packet from ns1 -> ns2 (ICMP payload 1472 => 1500-byte inner IP packets)"
sudo ip netns exec ns1 ping -M do -c 4 -s 1472 192.168.200.2 > "$LOGDIR/ping.out" 2>&1 || true
cat "$LOGDIR/ping.out"
if grep -q '0% packet loss' "$LOGDIR/ping.out"; then
  echo "PING DF: success (inner 1500-byte IP packet traversed)"
else
  echo "PING DF: failed or partial loss"
fi

# optional iperf3 throughput test (if iperf3 installed)
if command -v iperf3 >/dev/null 2>&1; then
  echo "Starting iperf3 server in ns2"
  sudo ip netns exec ns2 iperf3 -s > "$LOGDIR/iperf-server.log" 2>&1 & echo $! > "$LOGDIR/iperf-server.pid"
  sleep 1
  echo "Running iperf3 client from ns1 -> ns2 (10s)"
  sudo ip netns exec ns1 iperf3 -c 192.168.200.2 -t 10 -P 2 | tee "$LOGDIR/iperf-client.out"
  sudo kill "$(cat "$LOGDIR/iperf-server.pid")" || true
else
  echo "iperf3 not found; skipping throughput test"
fi

# cleanup etherip processes and namespaces
if [ -f "$LOGDIR/etherip-ns1.pid" ]; then sudo kill "$(cat "$LOGDIR/etherip-ns1.pid")" || true; fi
if [ -f "$LOGDIR/etherip-ns2.pid" ]; then sudo kill "$(cat "$LOGDIR/etherip-ns2.pid")" || true; fi
sudo "$SCRIPTDIR/setup_netns.sh" destroy

echo "Logs: $LOGDIR"
