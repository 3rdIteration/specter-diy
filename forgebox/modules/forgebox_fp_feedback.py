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
