#!/bin/bash
set -xe
ldconfig
systemctl daemon-reload 
systemctl enable zwo
rm -- "$0" # remove this script