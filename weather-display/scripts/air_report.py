"""Who is using the Wi-Fi air? Device report from the panel's air survey, with vendor names.

Usage:
    python scripts/air_report.py                      # last survey from http://weather7.local
    python scripts/air_report.py --run                # start a 20 s survey first, then report
    python scripts/air_report.py --host 10.0.11.141
    python scripts/air_report.py --file air.json      # a saved /api/air.json

Vendor names come from the IEEE registry (MA-L, MA-M and MA-S), downloaded once
into ~/.cache/oui/ and looked up locally: no device address is sent anywhere.
Randomised ("private") addresses - phones and tablets hiding their real MAC -
have no vendor by design; match them to devices in your router's client list.
"""
import argparse
import csv
import io
import json
import os
import time
import urllib.request

REGISTRIES = {  # Longest prefixes first when looking up.
    "MA-S": ("https://standards-oui.ieee.org/oui36/oui36.csv", 9),
    "MA-M": ("https://standards-oui.ieee.org/oui28/mam.csv", 7),
    "MA-L": ("https://standards-oui.ieee.org/oui/oui.csv", 6),
}
CACHE = os.path.join(os.path.expanduser("~"), ".cache", "oui")
MAX_AGE_DAYS = 30


def load_registry():
    """Return {hex prefix: organisation} from the cached IEEE CSVs (downloading if missing or old)."""
    os.makedirs(CACHE, exist_ok=True)
    table = {}
    for name, (url, _) in REGISTRIES.items():
        path = os.path.join(CACHE, f"{name}.csv")
        stale = not os.path.exists(path) or time.time() - os.path.getmtime(path) > MAX_AGE_DAYS * 86400
        if stale:
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "air-report/1.0"})
                with urllib.request.urlopen(req, timeout=60) as r, open(path, "wb") as f:
                    f.write(r.read())
            except OSError as e:
                if not os.path.exists(path):
                    print(f"(could not download {name} registry: {e})")
                    continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for row in csv.DictReader(f):
                table[row["Assignment"].upper()] = row["Organization Name"].strip()
    return table


def vendor(mac, table):
    if int(mac[:2], 16) & 0x02:
        return "(private / randomised address)"
    h = mac.replace(":", "").upper()
    for _, (_, n) in REGISTRIES.items():
        if h[:n] in table:
            return table[h[:n]]
    return "(unknown)"


def fetch(host, run):
    base = f"http://{host}"
    if run:
        req = urllib.request.Request(f"{base}/api/nettest", data=b"action=air", method="POST")
        urllib.request.urlopen(req, timeout=10).read()
        print("Listening for 20 s...")
        time.sleep(23)
    with urllib.request.urlopen(f"{base}/api/air.json", timeout=15) as r:
        return json.load(r)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="weather7.local")
    ap.add_argument("--run", action="store_true", help="start a new 20 s survey first")
    ap.add_argument("--file", help="read a saved /api/air.json instead")
    args = ap.parse_args()

    d = json.load(open(args.file)) if args.file else fetch(args.host, args.run)
    if "error" in d:
        raise SystemExit(d["error"])
    table = load_registry()

    a = d["airPct"]
    print(f"\nChannel {d['channel']}, {d['seconds']} s: air {d['busyPct']}% busy, {d['retryPct']}% re-sent; "
          f"{d['devices']} of your devices active, {d['neighbours']} neighbour devices")
    print("Air time: " + ", ".join(f"{k} {v}%" for k, v in a.items()))
    print("\nYour mesh units heard (2.4 GHz radios):")
    for u in d["units"]:
        print(f"  {u['mac']}  signal {u['rssi']:>4} dBm  {u['devices']:>3} devices  {u['airPct']:>5}% air  "
              f"{vendor(u['mac'], table)}")

    if d.get("broadcastPctByKind"):
        print("\nBroadcast/multicast your units repeat to everyone, by kind: "
              + ", ".join(f"{k} {v}%" for k, v in d["broadcastPctByKind"].items()))
        print("Devices whose broadcasts use the most air:")
        for s in d.get("broadcastSources", []):
            print(f"  {s['mac']}  {s['airPct']:>5}% air  {s['frames']:>5} frames  {vendor(s['mac'], table)}")

    print(f"\n{'MAC':17}  {'air%':>5}  {'kB':>6}  {'frames':>6}  {'dBm':>4}  {'Mbps':>5}  {'via unit':17}  vendor")
    for t in d["clients"]:
        flag = " [mesh unit]" if t.get("meshUnit") else ""
        print(f"{t['mac']}  {t['airPct']:>5}  {t['kB']:>6}  {t['frames']:>6}  {t.get('rssi', ''):>4}  "
              f"{t.get('maxMbps', ''):>5}  {t.get('via', ''):17}  {vendor(t['mac'], table)}{flag}")
    print("\nair% = share of the channel's air time. Slow (low Mbps) or weak (low dBm) devices use more air per byte.")


if __name__ == "__main__":
    main()
