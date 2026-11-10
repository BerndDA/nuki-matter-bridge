#!/bin/bash
# List the Matter nodes on the local network with host name, port and addresses (macOS, dns-sd).
#
#   tools/matter_hosts.sh
#
# One line per node and fabric: "<fabric id>-<node id>  <host>.local  port  <addresses>".
# A device paired with two controllers appears twice with the same host. Thread devices are
# announced by the border router and have only IPv6 addresses; this bridge is <Wi-Fi MAC>.local.
# dns-sd never exits on its own, so every lookup is stopped after a fixed wait (~5 s per node).
browse() { dns-sd -B _matter._tcp local. & local p=$!; sleep 3; kill $p; }
browse 2>/dev/null | awk '$2=="Add"{print $NF}' | sort -u | while read -r inst; do
  host=$( (dns-sd -L "$inst" _matter._tcp local. & p=$!; sleep 2; kill $p) 2>/dev/null \
          | sed -n 's/.*can be reached at \([^ ]*\)\.:\([0-9]*\).*/\1 \2/p' | head -1)
  set -- $host
  addrs=$( (dns-sd -G v4v6 "$1" & p=$!; sleep 2; kill $p) 2>/dev/null \
          | awk '$2=="Add" && $(NF-1) ~ /^[0-9A-Fa-f:.%]+/ {print $(NF-1)}' | sed 's/%.*//' | sort -u | tr '\n' ' ')
  printf '%-36s %-30s port %-5s %s\n' "$inst" "$1" "$2" "$addrs"
done
