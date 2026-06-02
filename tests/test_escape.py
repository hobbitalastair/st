import time

import pytest


@pytest.mark.st_cmd("printf '\\033[H\\033[2J\\033#8'; sleep 3")
def test_dectest(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("dectest")


@pytest.mark.st_cmd(
    "printf '\\033[H\\033[2J\\033[31mRED\\033[32mGREEN\\033[34mBLUE\\033[0m\\n'; sleep 3"
)
def test_sgr_colors(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("sgr_colors")


@pytest.mark.st_cmd("printf '\\033[H\\033[2J     \\033[5;5HX'; sleep 3")
def test_cursor_movement(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("cursor_move")


@pytest.mark.st_cmd("printf '\\033[H\\033[2Jhello\\033[2J'; sleep 3")
def test_clear_screen(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("clear_screen")


@pytest.mark.st_cmd(
    "printf '\033[?25l\033[H\033[2J"
    "\033[1mbold\033[0m \033[2mfaint\033[0m \033[3mitalic\033[0m \033[4munder\033[0m \033[9mstrike\033[0m\n"
    "\033[38;2;255;64;0mtruecolor\033[0m \033[48;5;25mindexed-bg\033[0m \033[7mreverse\033[0m \033[8minvisible\033[0m\n"
    "wide: 表語 emoji: \360\237\230\200 combining: e\314\201\n"
    "\033[?25h\033[3 q\033[5 q'; sleep 3"
)
def test_rich_rendering_sequences_do_not_crash(st_instance):
    time.sleep(1.5)
    st_instance.screenshot("rich_rendering_sequences")
    st_instance.assert_running()
