Ili9488 UI touch example
------------------------

Below is a minimal example showing how to supply a `get_touch()` callback
to `ScreenManager` and how a widget can implement `on_touch(touch)` to
receive touch events. The `get_touch` callable should return an `(x, y)`
tuple when the screen is being touched, or `None` when there is no touch.

```python
import uasyncio as asyncio
import ili9488
import ports.esp32.modules.ili9488_ui as ui

# initialize display/driver as appropriate for your port
disp = ili9488
sm = ui.ScreenManager(disp)

screen = ui.Screen('main')

class MyButton(ui.Button3D):
    # receive a Touch object with .x, .y and .pressed
    def on_touch(self, touch):
        if touch.pressed:
            self.set_pressed(True)
        else:
            self.set_pressed(False)

btn = MyButton(10, 10, 120, 40, 'Click')
screen.widgets.append(btn)
sm.add_screen(screen)
sm.goto_screen('main')

# user-provided polling function;  
def get_touch():
    # return (x, y) when touched, or None when not touched
    # e.g. return ili9488.read_touch() or read from your touch controller
    return None

sm.set_get_touch(get_touch)

# run asyncio loop (if your application uses it)
loop = asyncio.get_event_loop()
loop.run_forever()
```
