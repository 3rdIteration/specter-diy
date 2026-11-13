import forgebox
from binascii import hexlify
from hosts import Host


class DiagnosticsHost(Host):
    button = 'Device diagnostics'
    RECOVERY_TIME = 0

    @property
    def is_enabled(self):
        return self.manager is not None and self.manager.keystore.fingerprint is not None

    async def get_data(self):
        board, tamper, fingerprint, ds28, atecc = forgebox.device_diagnostics()
        battery = forgebox.battery_status()
        battery_text = 'Unavailable'
        if battery is not None:
            level, charging, millivolts = battery
            battery_text = '%d%% (%d mV)%s' % (level, millivolts, ', charging' if charging else '')
        cpu_debug = forgebox.cpu_debug_status()
        debug_text = 'CPU debug status unavailable'
        if cpu_debug is not None:
            jtag_control, halt_enabled = cpu_debug
            debug_text = 'JTAG control register 0x%08x\nCore halting debug %s' % (
                jtag_control, 'enabled' if halt_enabled else 'clear')
        capacity = forgebox.wallet_spi_capacity()
        jedec = forgebox.wallet_spi_jedec_id()
        spi_text = '%d MiB (JEDEC %06x)' % (capacity // (1024 * 1024), jedec) if capacity else (
            'Unknown capacity (JEDEC %06x)' % jedec)
        await self.manager.gui.alert('Device diagnostics',
            'Board: %s\n'
            'Battery: %s\n'
            'SPI flash: %s total\n\n'
            'DS28S60 block 31 protection: %s\n'
            'ATECC revision: %s\n\n'
            'Tamper input (PA2): %s\n'
            'Fingerprint IRQ (PF14): %s\n\n'
            '%s' %
            (board, battery_text, spi_text,
             '0x%02x' % ds28 if ds28 is not None else 'No valid reply',
             hexlify(atecc).decode() if atecc is not None else 'No valid reply',
             'High (normal input)' if tamper else 'Low (tamper input active)',
             'High' if fingerprint else 'Low',
             debug_text))
