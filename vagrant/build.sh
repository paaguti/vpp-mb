#!/bin/bash

# Install VPP v17.10 (Ubuntu 16.04 Xenial)
echo "deb [trusted=yes] https://nexus.fd.io/content/repositories/fd.io.stable.1710.ubuntu.xenial.main/ ./" | tee -a /etc/apt/sources.list.d/99fd.io.list
apt-get update
apt-get install -y git vpp vpp-lib vpp-dev autoconf libtool traceroute python-scapy iperf3 ethtool

# Compile/Install MMB plugin
service vpp stop
(cd /home/vagrant/vpp-mb/mmb-plugin; autoreconf -fis; ./configure; make; make install)

