#!/usr/bin/env python3
"""Persist history on a LAN/VPN computer without writing ESP8266 flash."""
import argparse, base64, csv, getpass, json, time, urllib.request
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('url',help='http://ESP8266-IP over trusted LAN or VPN')
p.add_argument('--user',default='admin'); p.add_argument('--interval',type=int,default=30)
p.add_argument('--output',type=Path,default=Path('solar-history.csv'))
p.add_argument('--hours',type=float,default=24,help='Stop after this duration')
a=p.parse_args()
if a.interval<10 or a.hours<=0: p.error('interval >=10 seconds and hours >0 required')
token=base64.b64encode(f'{a.user}:{getpass.getpass("Web password: ")}'.encode()).decode()
end=time.monotonic()+a.hours*3600; previous=None
new=not a.output.exists()
with a.output.open('a',newline='') as f:
    writer=csv.writer(f)
    if new: writer.writerow(['received_unix','uptime_s','pv_W','load_W','battery_charge_W','temperature_C','humidity_percent','commanded_on','state_known'])
    while time.monotonic()<end:
        try:
            req=urllib.request.Request(a.url.rstrip('/')+'/api/history',headers={'Authorization':'Basic '+token})
            with urllib.request.urlopen(req,timeout=10) as response: rows=json.load(response)
            # Uptime decreasing indicates reboot; reconnect gracefully without duplicate batches.
            if rows and previous is not None and rows[-1]['s']<previous: previous=None
            for row in rows:
                if previous is not None and row['s']<=previous: continue
                writer.writerow([int(time.time()),row['s'],row['pv'],row['load'],row['battery'],row['t'],row['h'],row['on'],row['known']]); previous=row['s']
            f.flush()
        except (OSError,ValueError,KeyError) as error: print('Retry:',error)
        time.sleep(a.interval)
