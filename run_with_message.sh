#!/bin/sh
# Demo: pipe a greeting into the guest's COM1 (serial) and watch the kernel reply on VGA.
#set -euo pipefail

cat <<'BANNER'

┌─────────────────────────────────────────────────────────┐
│  You can send text to SkezOS by piping stdin to QEMU's  │
│  COM1 (serial) and watch the kernel echo it back.       │   
|  This is already configured in the makefile.            |
└─────────────────────────────────────────────────────────┘

BANNER

echo "joe@joe-ThinkPad-T480:~/SkezOZ$ printf 'Hi Joe\\\\n | make run'"
#echo "Pipes 'Hi Joe' into QEMU's serial console (COM1). The guest kernel receives this as serial input, prints on the VGA display."

cat <<'FOOTER'

┌────────────────────────────────────────────────────────┐
│  Once the guest is running, type into the QEMU window. │
│  The VM's keyboard is bridged to the serial console    │
│  (via -serial mon:stdio), so the kernel sees input     │
│  from both the serial line and the VGA/keyboard window.│
└────────────────────────────────────────────────────────┘
FOOTER
printf 'Hi Joe\n' | make run