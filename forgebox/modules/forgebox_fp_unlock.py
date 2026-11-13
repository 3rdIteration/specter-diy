import forgebox
import platform
from errors import BaseError
from forgebox_fp_feedback import clear, describe
from forgebox_pin import FAILURE_LIMIT, derive_pin_key
from gui.screens import Alert, Menu, Prompt
from helpers import tagged_hash


def scan_feedback(status):
    if status == 0x81:
        return 'Fingerprint not recognized', 'Use the enrolled finger or enter your startup PIN.'
    if status in (0x11, 0x14, 0x17, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x50):
        return 'Could not read fingerprint', 'Cover the sensor with your fingertip and hold it still.'
    if status == -6:
        return 'Fingerprint scan timed out', 'Touch the sensor when the scanning screen appears.'
    if status == 0x01 or -163 <= status <= -101:
        return 'Scan inconclusive', 'Lift your finger and try again, or use your startup PIN.'
    return 'Fingerprint unavailable', 'Try again or use your startup PIN.'


class FingerprintLogin:
    def __init__(self, store):
        self.store = store
        self.remaining = 3 if forgebox.fp_unlock_test(0, None, None) == 2 else 0

    async def unlock(self):
        store = self.store
        while True:
            buttons = [(0, 'Use startup PIN')]
            if self.remaining:
                buttons.insert(0, (1, 'Use fingerprint'))
            choice = await store.show(Menu(buttons, title='Unlock wallet', last=(2, 'Power off')))
            if choice == 2:
                try:
                    platform.power_off()
                except BaseError as error:
                    await store.show(Alert('Power off', str(error)))
                continue
            if choice != 1:
                return False
            self.remaining -= 1
            store.show_backup_status('Scanning fingerprint', 'Touch the enrolled finger now.')
            result = forgebox.fp_unlock_test(2, None, self.show_match)
            if isinstance(result, bytearray):
                try:
                    store.secret = bytes(result[:32])
                    store.enc_secret = tagged_hash('enc', store.secret)
                    store.seed_pin_context = result[32:]
                    store.failures = 0
                    return True
                finally:
                    clear(result)
            if result == -210:
                await store.show(Alert('Wallet storage wiped', 'The PIN attempt limit was reached.'))
                platform.reboot()
                return False
            if -218 <= result < -210:
                store.failures = FAILURE_LIMIT - (-210 - result)
                self.remaining = 0
            if self.remaining:
                await store.show(Alert(*scan_feedback(result)))
            else:
                title, _ = scan_feedback(result)
                await store.show(Alert('Startup PIN required', title + '.\n\n'
                                      'Use your startup PIN to unlock.'))

    def show_match(self, steps, status):
        self.store.show_backup_status('Fingerprint recognized', 'Unlocking wallet...')


async def fingerprint_settings(store):
    state = forgebox.fp_unlock_test(0, None, None)
    if state in (1, 2):
        accepted = await store.show(Prompt('Disable fingerprint unlock',
            'Revoke fingerprint access and delete the enrolled template.',
            confirm_text='Disable'))
        if accepted:
            store.show_backup_status('Disabling fingerprint', 'Revoking access and removing the template.')
            status = forgebox.fp_unlock_test(3, None, None)
            await store.show(Alert('Fingerprint unlock', 'Disabled and template removed.' if status == 0 else
                'Cleanup could not be verified. Check again or use the startup PIN.'))
        return
    if state != 0:
        await store.show(Alert('Fingerprint unavailable', 'Fingerprint storage could not be verified.'))
        return
    store.show_backup_status('Checking fingerprint sensor', 'Reading the template count.')
    count = forgebox.fp_unlock_test(4, None, None)
    if count < 0:
        await store.show(Alert('Fingerprint sensor unavailable', describe(count)))
        return
    if count > 1:
        await store.show(Alert('Fingerprint enrollment unavailable',
                               'The sensor already holds %d templates.' % count))
        return
    replace = count == 1
    if replace:
        accepted = await store.show(Prompt('Replace old fingerprint?',
            'The sensor still holds a fingerprint that is not linked to this wallet. '
            'Enter your startup PIN to remove it and enroll a new finger.',
            confirm_text='Replace and enroll'))
    else:
        accepted = await store.show(Prompt('Enroll a fingerprint',
            'Keep your startup PIN. Either the enrolled finger or the PIN can unlock this wallet.',
            confirm_text='Enroll'))
    if not accepted:
        return
    await store.wait_before_attempt()
    pin = await store.enter_pin('Authorize fingerprint enrollment', cancel=True)
    if pin is None:
        return
    pin_key = bytearray(derive_pin_key(pin, store.salt))
    pin = None

    def progress(steps, status):
        message = 'Lift and reposition the same finger.'
        if status:
            message += '\nModule status 0x%02x.' % status
        store.show_backup_status('Enrollment %d / 8' % steps, message)

    try:
        store.show_backup_status('Enrollment 0 / 8', 'Touch and lift the same finger.')
        status = forgebox.fp_unlock_test(5 if replace else 1, pin_key, progress)
    finally:
        clear(pin_key)
    if status == -210:
        await store.show(Alert('Wallet storage wiped', 'The PIN attempt limit was reached.'))
        platform.reboot()
        return
    if -218 <= status < -210:
        store.failures = FAILURE_LIMIT - (-210 - status)
    elif status == 0:
        store.failures = 0
    if status == -205:
        await store.show(Alert('Enrollment failed', 'The sensor template count changed. Open fingerprint settings again.'))
        return
    if status == -206:
        await store.show(Alert('Enrollment failed', 'The old template could not be removed. Open fingerprint settings to retry cleanup.'))
        return
    message = 'Power off and turn the device back on to test fingerprint unlock.' if status == 0 else (
        'The fingerprint module or its storage was unavailable.' if status == -204 else describe(status))
    await store.show(Alert('Fingerprint enrolled' if status == 0 else 'Enrollment failed', message))
