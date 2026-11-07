#!/usr/bin/env python3
"""Firmware update over Wi-Fi: upload a .bin to the bridge's /api/ota and wait for it to come back.

    tools/ota.py <host> <firmware.bin> <sdkconfig>

The OTA password is read from CONFIG_NUKI_OTA_PASSWORD in the given sdkconfig.
Usually called via `make ota`.
"""

import json
import re
import sys
import time
import urllib.error
import urllib.request


def password_from(sdkconfig):
    with open(sdkconfig) as f:
        m = re.search(r'^CONFIG_NUKI_OTA_PASSWORD="(.*)"$', f.read(), re.M)
    if not m or not m.group(1):
        sys.exit(f'No CONFIG_NUKI_OTA_PASSWORD in {sdkconfig}. Set it with `make menuconfig` '
                 '(Nuki Matter Bridge) and flash once over USB.')
    return m.group(1)


def status(host, timeout=4):
    with urllib.request.urlopen(f'http://{host}/api/status', timeout=timeout) as r:
        return json.load(r)['system']


class ProgressReader:
    """File-like body that prints upload progress."""

    def __init__(self, data):
        self.data, self.pos = data, 0

    def read(self, n=-1):
        n = len(self.data) - self.pos if n < 0 else n
        chunk = self.data[self.pos:self.pos + n]
        self.pos += len(chunk)
        pct = self.pos * 100 // len(self.data)
        print(f'\r  uploading {pct:3d}%  ({self.pos // 1024} / {len(self.data) // 1024} KB)', end='', flush=True)
        return chunk

    def __len__(self):
        return len(self.data)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    host, image_path, sdkconfig = sys.argv[1:]
    password = password_from(sdkconfig)
    with open(image_path, 'rb') as f:
        image = f.read()

    try:
        before = status(host)
    except OSError as e:
        sys.exit(f'Bridge not reachable at {host}: {e}')
    print(f'Bridge at {host}: running {before["version"]} from {before["partition"]}')

    req = urllib.request.Request(
        f'http://{host}/api/ota', data=ProgressReader(image), method='POST',
        headers={'X-OTA-Password': password, 'Content-Type': 'application/octet-stream',
                 'Content-Length': str(len(image))})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            reply = json.load(r)
    except urllib.error.HTTPError as e:
        print()
        try:
            msg = json.load(e)['message']
        except Exception:
            msg = e.reason
        sys.exit(f'Update rejected ({e.code}): {msg}')
    print(f'\n  {reply["message"]} ({time.time() - t0:.0f} s)')

    print('  waiting for the bridge', end='', flush=True)
    time.sleep(3)
    for _ in range(40):
        try:
            after = status(host, timeout=2)
            if after['uptimeS'] < 60:
                break
        except OSError:
            pass
        print('.', end='', flush=True)
        time.sleep(2)
    else:
        sys.exit('\nBridge did not come back within ~90 s. Check the serial log (`make monitor`).')
    print()

    if after['partition'] == before['partition']:
        sys.exit(f'Bridge is still running from {after["partition"]}: the new firmware was rolled back. '
                 'Check the serial log.')
    print(f'Done: running {after["version"]} from {after["partition"]} (reset: {after["resetReason"]})')


if __name__ == '__main__':
    main()
