import time

import pytest

from .helpers import _pixel_channels, _read_png


@pytest.mark.st_cmd("printf '\\033[H\\033[2J\\360\\237\\230\\200 \\360\\237\\216\\211 \\360\\237\\224\\245\\n'; sleep 3")
def test_emoji_basic(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("emoji_basic")


@pytest.mark.st_cmd("printf '\033[?25l\033[H\033[2J\033[8m\360\237\230\200\033[0m'; sleep 3")
def test_invisible_emoji_does_not_render_color_glyph(st_instance):
    time.sleep(1.5)
    captured = st_instance.screenshot("invisible_emoji")
    width, height, bit_depth, color_type, raw = _read_png(captured)
    pixels = _pixel_channels(raw, color_type, bit_depth, width, height)

    saturated = 0
    for pixel in pixels:
        r, g, b = pixel[:3]
        if max(r, g, b) > 80 and max(r, g, b) - min(r, g, b) > 50:
            saturated += 1

    assert saturated == 0
