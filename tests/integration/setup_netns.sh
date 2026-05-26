#!/bin/bash
set -eu
ACTION=${1:-create}
NS1=ns1
NS2=ns2

ensure_netns_mountpoint() {
  if [ ! -c /dev/net/tun ]; then
    echo "Missing /dev/net/tun; rebuild the devcontainer after adding --device=/dev/net/tun" >&2
    exit 1
  fi

  mkdir -p /run/netns
  mount --make-shared /run/netns 2>/dev/null || true
}

create() {
  ensure_netns_mountpoint

  ip netns del $NS1 2>/dev/null || true
  ip netns del $NS2 2>/dev/null || true

  ip netns add $NS1
  ip netns add $NS2

  ip link add veth1 type veth peer name veth2
  ip link set veth1 netns $NS1
  ip link set veth2 netns $NS2

  ip netns exec $NS1 ip link set lo up
  ip netns exec $NS2 ip link set lo up

  ip netns exec $NS1 ip link set dev veth1 up
  ip netns exec $NS2 ip link set dev veth2 up

  ip netns exec $NS1 ip addr add 10.10.10.1/24 dev veth1
  ip netns exec $NS2 ip addr add 10.10.10.2/24 dev veth2

  ip netns exec $NS1 ip tuntap add dev tap1 mode tap
  ip netns exec $NS2 ip tuntap add dev tap2 mode tap
  ip netns exec $NS1 ip link set dev tap1 up
  ip netns exec $NS2 ip link set dev tap2 up
  ip netns exec $NS1 ip addr add 192.168.200.1/24 dev tap1
  ip netns exec $NS2 ip addr add 192.168.200.2/24 dev tap2

  echo "created namespaces and interfaces"
}

destroy() {
  ip netns del $NS1 || true
  ip netns del $NS2 || true
  echo "deleted namespaces"
}

case "$ACTION" in
  create) create ;;
  destroy) destroy ;;
  *) echo "Usage: $0 {create|destroy}"; exit 1 ;;
esac
