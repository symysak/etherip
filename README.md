# etherip
EtherIP(RFC3378) for Linux

Confirmed to work with NEC UNIVERGE IX series

## install
```
make
sudo make install
```
## Usage
```
etherip -h

// example
sudo ip tuntap add mode tap dev tap0
sudo ip link set up dev tap0
sudo ip addr add 192.168.1.2/24 dev tap0
sudo etherip ipv4 dst 192.168.1.1 src 192.168.1.2 tap tap0
```

## uninstall
```
sudo make remove
```
