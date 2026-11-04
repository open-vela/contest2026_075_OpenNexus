"""Enable the reversible FocusMate SF32LB52 onboard BLE bring-up profile.

Uses the vendor LCPU/IPC HCI implementation through UART_BTH4. The first
profile uses legacy advertising and unauthenticated diagnostic text; no
recordings, personal tasks or API credentials are sent during bring-up.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace',type=Path)
parser.add_argument('--cache-disabled-at-boot',action='store_true',help='Diagnostic: retain I-cache; disable D-cache from boot via SiFli Kconfig option')
parser.add_argument('--cache-write-through',action='store_true',
                    help='EXPERIMENTAL: FORCEWT failed board verification; do not use as working profile')
args=parser.parse_args()
root=args.workspace.resolve()
build=root/'cmake_out/sf32lb52_devkit_lcd'
config=build/'.config'
if not config.exists() or 'CONFIG_ARCH_CHIP_SF32LB52=y' not in config.read_text():
    parser.error('existing SF32LB52 build required')
backup=build/'.config.pre-focusmate-ble'
if not backup.exists():shutil.copyfile(config,backup)
symbols={
    'CONFIG_DRIVERS_BLUETOOTH':'y','CONFIG_UART_BTH4':'y',
    'CONFIG_DEBUG_MM':'y',
    'CONFIG_UART_BTH4_RXBUFSIZE':'8096','CONFIG_UART_BTH4_TXBUFSIZE':'2048',
    'CONFIG_BT':'y','CONFIG_BT_ORIGINAL_API':'y','CONFIG_BT_H4':'y',
    'CONFIG_BT_PERIPHERAL':'y','CONFIG_BT_BROADCASTER':'y',
    'CONFIG_BT_CENTRAL':'y','CONFIG_BT_OBSERVER':'y','CONFIG_BT_CLASSIC':'y',
    # This vendor Zblue identity implementation accesses IRKs even when
    # privacy is disabled; retain the supported baseline security configuration.
    'CONFIG_BT_EXT_ADV':'n','CONFIG_BT_PRIVACY':'y','CONFIG_BT_SMP':'y',
    'CONFIG_BT_SCAN_AND_INITIATE_IN_PARALLEL':'n',
    'CONFIG_BT_GATT_DYNAMIC_DB':'y','CONFIG_BT_DEVICE_NAME_DYNAMIC':'y',
    # Start with an uncached diagnostic database. The vendor MC hash worker
    # has not passed board stability testing; it is unnecessary for NUS I/O.
    'CONFIG_BT_GATT_CACHING':'n',
    'CONFIG_BT_DEVICE_NAME':'"FocusMate"','CONFIG_BT_MAX_CONN':'1',
    'CONFIG_BT_PERIPHERAL_PREF_TIMEOUT':'400',
}
# Mailbox read/write indices share cache lines across CPUs. Write-back
# cleaning can overwrite the peer's field. Keep reads cached with explicit
# invalidation, while write-through avoids dirty shared-line writeback.
# Opt-in so existing builds retain their configuration until requested.
if args.cache_write_through:
    symbols['CONFIG_ARMV8M_DCACHE']='y'
    symbols['CONFIG_ARMV8M_DCACHE_WRITETHROUGH']='y'
if args.cache_disabled_at_boot:
    if args.cache_write_through:
        parser.error('cache modes are mutually exclusive')
    symbols.update(CONFIG_SF32LB52_DCACHE_BOOT_DISABLED='y',
                   CONFIG_ARMV8M_DCACHE='n', CONFIG_ARCH_DCACHE='n',
                   CONFIG_ARMV8M_DCACHE_WRITETHROUGH='n')
lines=config.read_text().splitlines()
for name,value in symbols.items():
    lines=[l for l in lines if not (l.startswith(name+'=') or l=='# '+name+' is not set')]
    lines.append('# '+name+' is not set' if value=='n' else name+'='+value)
config.write_text('\n'.join(lines)+'\n')
env=os.environ.copy()
env['PATH']=str(Path.home()/'.local/bin')+os.pathsep+env['PATH']
env.update(KCONFIG_CONFIG=str(config),EXTERNALDIR='dummy',APPSDIR=str(root/'apps'),
           DRIVERS_PLATFORM_DIR='dummy',APPSBINDIR=str(build/'apps'),BINDIR=str(build))
tool=shutil.which('olddefconfig',path=env['PATH'])
if not tool:parser.error('olddefconfig not available')
with (build/'focusmate-ble-config.log').open('w') as out:
    subprocess.run([tool],cwd=root/'nuttx',env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
actual=config.read_text()
for name in ('CONFIG_UART_BTH4','CONFIG_BT','CONFIG_BT_ORIGINAL_API','CONFIG_BT_H4'):
    if name+'=y' not in actual:raise SystemExit(name+' was not enabled; inspect dependencies')
if 'CONFIG_BT_GATT_CACHING=y' in actual:
    raise SystemExit('GATT caching is still enabled; diagnostic isolation was not applied')
if 'CONFIG_BT_EXT_ADV=y' in actual:
    raise SystemExit('Extended advertising is still enabled; legacy diagnostic profile required')
# Stack sizes are hidden, derived Kconfig values in this vendor tree.
# Do not claim requested .config overrides survived olddefconfig.
print('BLE HCI profile enabled; original config preserved at',backup)
print('Next: cmake -S nuttx -B cmake_out/sf32lb52_devkit_lcd -DFOCUSMATE_BLE_RELAY=ON')

if args.cache_disabled_at_boot and 'CONFIG_ARMV8M_DCACHE=y' in actual:
    raise SystemExit('D-cache is still forced on; apply the SiFli Kconfig patch first')
