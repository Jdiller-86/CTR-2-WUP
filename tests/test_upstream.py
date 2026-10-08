from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from host.errors import HostError
from translator.portable import adapt_source
from translator.upstream import generate


class PortableAdapterTests(unittest.TestCase):
    def test_instruction_state_and_removed_fast_paths(self):
        source='''void f_00100000(Context *ctx) {
    uint32_t reg0_ = ctx->r[0];
    uint32_t reg15_ = ctx->r[15];
    int32_t budget_ = ctx->budget;
#define BUDGET_LEFT budget_
    uint32_t target = reg15_, resume = 0;
dispatch:
L_00100000:
    BLOCK_TO(0x00100000u, 2, out_of_budget_0);
    /* 00100000 E5900000 */
    reg0_ = mem_read32(ctx, reg0_);
    /* 00100004 EF000003 */
    SVC(0x00100008u, 3);
    OUT_OF_BUDGET_AT(out_of_budget_0);
}'''
        adapted,count=adapt_source(source)
        self.assertEqual(count,2)
        self.assertIn('ctx->r[0] = mem_read32(ctx, ctx->r[0]);',adapted)
        self.assertIn('CTR_INSTRUCTION(0x00100000u, 0xE5900000u);',adapted)
        self.assertNotIn('BLOCK_TO',adapted)

    def test_unknown_emitter_grammar_fails(self):
        for source in ('void unknown(void) {}','/* 00100000 E1A00000 */\nreg17_ = 0;',
                       '    /* 00100000 E1A00000 */\nread_pages_ = NULL;',
                       '    /* 00100000 E1A00000 */\nvfp33_ = 0;'):
            with self.assertRaises(ValueError):adapt_source(source)

    def test_unverified_driver_does_not_run_or_publish(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);binary=root/'unknown-generator';binary.write_bytes(b'not an installed driver')
            with patch('translator.upstream.checked_source'),patch('host.build._run_build') as command:
                with self.assertRaises(HostError) as caught:
                    generate('unused.cxi',root/'output',generator=binary)
            self.assertEqual(caught.exception.code,'unverified_generator')
            command.assert_not_called();self.assertFalse((root/'output').exists())
            self.assertFalse(list(root.glob('.ctr2wup-lifter-*')))

if __name__=='__main__':unittest.main()
