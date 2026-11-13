import forgebox
import platform
from forgebox_pin import FAILURE_LIMIT, derive_pin_key
from gui.screens import Alert, Prompt
from helpers import tagged_hash


def describe(status):
    if status == -200:
        return 'The match key could not open the test credential.'
    if status == -204:
        return 'The test template or credential was unavailable.'
    if status == -208:
        return 'Gen 1 storage could not verify the unlock.'
    if -218 <= status < -210:
        return 'PIN check failed. %d attempts left.' % (-210 - status)
    if -163 <= status < -100:
        return 'Module reply had %d bytes. A key-bearing reply needs 38.' % (-100 - status)
    if status > 0:
        return 'Fingerprint module status 0x%02x.' % status
    return 'Fingerprint transport status %d.' % status


def clear(data):
    if isinstance(data, bytearray):
        for i in range(len(data)):
            data[i] = 0


async def fingerprint_unlock_test(signer):
    store = signer.keystore
    accepted = await store.show(Prompt(
        'Fingerprint unlock test',
        'Use a disposable Gen 1 wallet. The test enrolls one finger, tries two scans, '
        'and deletes the template. Have your startup PIN ready.'))
    if not accepted:
        return
    store.show_backup_status('Checking fingerprint module', 'Reading the template count.')
    count = forgebox.fp_unlock_test(0, None, None)
    if count == 1:
        if await store.show(Prompt('One template already exists',
                                   'Delete slot 0 only if it was left by your previous test.',
                                   confirm_text='Delete test slot')):
            count = forgebox.fp_unlock_test(4, None, None)
            await store.show(Alert('Test template cleanup',
                                   'Verified zero templates.' if count == 0 else
                                   'Could not verify cleanup. Count result: %d.' % count))
        return
    if count != 0:
        await store.show(Alert('Test unavailable', 'Template count result: %d.' % count))
        return

    pin = await store.enter_pin('Authorize test enrollment', cancel=True)
    if pin is None:
        return
    pin_key = bytearray(derive_pin_key(pin, store.salt))
    pin = None
    expected = tagged_hash('ForgeBox fingerprint unlock test root', store.secret)

    def progress(steps, status):
        text = 'Lift and reposition the same finger.'
        if status:
            text += '\nModule status 0x%02x.' % status
        store.show_backup_status('Enrollment %d / 8' % steps, text)

    locked = False
    recovered = None
    message = 'Test stopped.'
    cleanup = None
    try:
        store.show_backup_status('Enrollment 0 / 8', 'Touch and lift the same finger.')
        status = forgebox.fp_unlock_test(1, pin_key, progress)
        clear(pin_key)
        if status == -210:
            platform.reboot()
            return
        if status:
            if -218 <= status < -210:
                store.failures = FAILURE_LIMIT - (-210 - status)
            message = describe(status)
        else:
            await signer.lock()
            locked = True
            if await store.show(Prompt('Different finger first',
                                       'Lift the enrolled finger. Tap Start scan and use a different finger.',
                                       confirm_text='Start scan')):
                store.show_backup_status('Scanning', 'Touch with a different finger now.')
                wrong = forgebox.fp_unlock_test(2, None, None)
                if isinstance(wrong, bytearray):
                    clear(wrong)
                    message = 'Test failed. The different-finger step unlocked Gen 1 storage.'
                elif wrong in (0x01, 0x81):
                    if await store.show(Prompt('Different finger denied',
                                               'Tap Start scan and touch with the enrolled finger.',
                                               confirm_text='Start scan')):
                        store.show_backup_status('Scanning', 'Touch with the enrolled finger now.')
                        result = forgebox.fp_unlock_test(2, None, None)
                        if isinstance(result, bytearray):
                            if tagged_hash('ForgeBox fingerprint unlock test root', result[:32]) == expected:
                                recovered = result
                                message = 'Enrolled finger unlocked the same Gen 1 wallet key.'
                            else:
                                clear(result)
                                message = 'Test failed. The recovered wallet key differed.'
                        else:
                            message = describe(result)
                else:
                    message = 'Different-finger step inconclusive. ' + describe(wrong)
    finally:
        clear(pin_key)
        try:
            store.show_backup_status('Cleaning up fingerprint test', 'Checking the template count.')
        finally:
            cleanup = forgebox.fp_unlock_test(3, None, None)

    if recovered is not None:
        store.secret = bytes(recovered[:32])
        store.enc_secret = tagged_hash('enc', store.secret)
        store.seed_pin_context = recovered[32:]
        store.failures = 0
        clear(recovered)
    message += ('\n\nCleanup verified. 0 test templates.' if cleanup == 0 else
                '\n\nCleanup unverified. Count result: %d.' % cleanup)
    await store.show(Alert('Fingerprint unlock test', message))
    if locked:
        await signer.unlock()
