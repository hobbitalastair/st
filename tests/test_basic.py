import time

import pytest


@pytest.mark.st_cmd("printf '\\033[H\\033[2Jhello st\\n'; sleep 3")
def test_st_renders_text(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("hello_text")


@pytest.mark.st_cmd("sleep 3")
def test_st_launches(st_instance):
    time.sleep(1.5)
    st_instance.assert_screenshot("blank_terminal")


@pytest.mark.st_cmd("cat")
def test_st_echoes_typed_input(st_instance):
    st_instance.type("hello st\n")
    time.sleep(1.5)
    st_instance.assert_screenshot("echo_typed")


@pytest.mark.st_cmd("cat")
def test_click_after_compositor_sized_launch_does_not_crash(st_instance):
    st_instance.pointer_click()
    time.sleep(0.5)
    st_instance.assert_running()


@pytest.mark.st_cmd("cat")
@pytest.mark.parametrize("button", ["left", "middle", "right"])
def test_pointer_buttons_do_not_crash(st_instance, button):
    st_instance.pointer_click(button)
    time.sleep(0.5)
    st_instance.assert_running()


@pytest.mark.st_cmd("cat")
@pytest.mark.parametrize("dy,dx", [(1, 0), (-1, 0), (0, 1), (0, -1)])
def test_pointer_scroll_does_not_crash(st_instance, dy, dx):
    st_instance.pointer_scroll(dy, dx)
    time.sleep(0.5)
    st_instance.assert_running()
