from io import BytesIO
from unittest import TestCase

from apps.wallets.manager import WalletManager, ADD_WALLET


class WalletManagerParseStreamTest(TestCase):
    def setUp(self):
        self.manager = WalletManager("test_wallet_manager")

    def test_descriptor_identified_as_wallet(self):
        descriptor = (
            "wsh(or_d(pk([73c5da0a/48'/0'/0'/2']xpub6DkFAXWQ2dHxq2vatrt9qyA3bXYU4ToWQwCHbf5XB2mSTexcHZCeKS1VZYcPoBd5X8yVcbXFHJR9R8UCVpt82VX1VhR28mCyxUFL4r6KFrf/<0;1>/*),"
            "and_v(v:pkh([5436d724/48'/0'/1'/2']xpub6DpyWrXE3rniBaesePU6FQ6Tg3KrPfnwJ2xidUueKh5ktrdn8QHB6uKs51reFrmzstEpfHjko8R8Rpp5YnYLZAaYbWX7A5nkyExNZzVr4Em/<0;1>/*),"
            "older(52596))))#gu0nfmxj"
        )
        stream = BytesIO(descriptor.encode())

        command, result_stream = self.manager.parse_stream(stream)

        self.assertEqual(command, ADD_WALLET)
        self.assertEqual(result_stream.read(), descriptor.encode())
