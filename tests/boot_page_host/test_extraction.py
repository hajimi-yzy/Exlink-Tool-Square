"""The extracted preview must reject geometry changes hidden in file context."""
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from run import assert_native_geometry, assert_production_context


class ProductionContextTests(unittest.TestCase):
    def test_plain_native_lvgl_calls_are_allowed(self):
        assert_native_geometry("void page(void) { lv_obj_set_pos(obj, 12, 150); }")

    def test_object_like_macro_is_rejected_even_for_parenthesized_calls(self):
        source = "#define lv_obj_set_pos ui_obj_set_pos\n(lv_obj_set_pos)(obj, 12, 150);"
        with self.assertRaisesRegex(ValueError, "redefines LVGL geometry"):
            assert_native_geometry(source)

    def test_function_like_macro_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "redefines LVGL geometry"):
            assert_native_geometry("# define lv_obj_set_size(o,w,h) scale(o,w,h)")

    def test_runtime_wrapper_is_rejected_without_a_macro(self):
        with self.assertRaisesRegex(ValueError, "runtime UI geometry scaling"):
            assert_native_geometry("lv_obj_set_width(obj, ui_scale_x(width));")

    def test_macro_hidden_in_production_header_is_rejected(self):
        with TemporaryDirectory() as temporary:
            repo = Path(temporary)
            (repo / "include").mkdir()
            (repo / "include/geometry.h").write_text("#define lv_obj_set_x scale_x\n")
            with self.assertRaisesRegex(ValueError, "include.*geometry.h redefines"):
                assert_production_context(repo)


if __name__ == "__main__":
    unittest.main()
