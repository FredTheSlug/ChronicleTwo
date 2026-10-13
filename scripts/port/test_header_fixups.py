import unittest
import header_fixups


class UnitFixups(unittest.TestCase):
    def test_invent_constructor_emitted_exactly_once(self):
        source = "// constructor\ninline CMenuInvent::CMenuInvent() {\n    card_cursor = 0;\n}\n"
        result = header_fixups.fixup_unit_text("inventmn.cpp", source)
        self.assertEqual(result, source.replace("inline CMenuInvent", "CMenuInvent"))
        with self.assertRaises(SystemExit):
            header_fixups.fixup_unit_text("inventmn.cpp", result)
        with self.assertRaises(SystemExit):
            header_fixups.fixup_unit_text("inventmn.cpp", source + source)
        self.assertEqual(header_fixups.fixup_unit_text("other.cpp", source), source)


if __name__ == "__main__":
    unittest.main()
