from picographics import PicoGraphics, DISPLAY_TUFTY_2040
from pimoroni import Button
from time import sleep
from jpegdec import JPEG
from pngdec import PNG
from machine import ADC, Pin, PWM
import math, random, gc, micropython
import random
from time import ticks_ms, ticks_diff

button_a = Button(7, invert=False)
button_b = Button(8, invert=False)
button_c = Button(9, invert=False)
button_up = Button(22, invert=False)
button_down = Button(6, invert=False)

led = Pin(25, Pin.OUT)
display = PicoGraphics(display=DISPLAY_TUFTY_2040)
j = PNG(display)
display.set_backlight(0.85)

LIGHTEST = display.create_pen(208, 208, 88)
LIGHT = display.create_pen(160, 168, 64)
DARK = display.create_pen(40, 40, 128)
DARKEST = display.create_pen(64, 80, 16)

WHITE = display.create_pen(255, 255, 255)
BLACK = display.create_pen(0, 0, 0)
RED = display.create_pen(255, 0, 0)

life_x = 80
life_y = 60
life_size = 4
life_frames = 500
lifegrid = [bytearray(life_x * life_y) for _ in range(2)]
change_mask = bytearray(life_x * life_y)
initial_dots_num = 1000

@micropython.viper
def calculate_generation(fnow: int, fnext: int):
    grid_now = ptr8(lifegrid[fnow])
    grid_next = ptr8(lifegrid[fnext])
    ly: int = 60
    lx: int = 80

    for x in range(1, lx-1):
        idx = x * ly
        for y in range(1, ly-1):
            idx_curr = idx + y
            # Inline neighbor access for speed
            idx_up = idx_curr - ly
            idx_down = idx_curr + ly

            # Count neighbors
            neighbors: int = 0
            if grid_now[idx_up - 1] == 1:
                neighbors += 1
            if grid_now[idx_up] == 1:
                neighbors += 1
            if grid_now[idx_up + 1] == 1:
                neighbors += 1
            if grid_now[idx_curr - 1] == 1:
                neighbors += 1
            if grid_now[idx_curr + 1] == 1:
                neighbors += 1
            if grid_now[idx_down - 1] == 1:
                neighbors += 1
            if grid_now[idx_down] == 1:
                neighbors += 1
            if grid_now[idx_down + 1] == 1:
                neighbors += 1

            if grid_now[idx_curr] == 1:  # alive
                if 2 <= neighbors <= 3:
                    grid_next[idx_curr] = 1
                else:
                    grid_next[idx_curr] = 2
            else:  # dead
                if neighbors == 3:
                    grid_next[idx_curr] = 1
                else:
                    grid_next[idx_curr] = 0

@micropython.viper
def mark_changes(fnow: int, fnext: int):
    grid_now = ptr8(lifegrid[fnow])
    grid_next = ptr8(lifegrid[fnext])
    mask = ptr8(change_mask)

    ly: int = 60
    lx: int = 80

    for x in range(lx):
        for y in range(ly):
            idx: int = x * ly + y
            if grid_now[idx] != grid_next[idx]:
                mask[idx] = grid_next[idx]
            else:
                mask[idx] = 255  # sentinel value for "no change"

@micropython.native
def collect_changes(fnext):
    # Collect changed cells from the mask
    white_cells = []
    red_cells = []
    black_cells = []

    for x in range(life_x):
        for y in range(life_y):
            idx = x * life_y + y
            val = change_mask[idx]
            if val != 255:  # changed
                if val == 1:
                    white_cells.append((x, y))
                elif val == 2:
                    red_cells.append((x, y))
                else:
                    black_cells.append((x, y))

    return white_cells, red_cells, black_cells

while True:
    random_number = random.randint(1, 72)
    led.value(1)
    j.open_file("pics/tufty"+str(random_number)+".png")
    j.decode()
    led.value(0)
    display.update()
    sleeptime=0
    while (sleeptime < 15):
        sleep(0.1)
        sleeptime = sleeptime + 0.1
        if (button_a.read()):
            sleeptime = 99
        if (button_b.read()):
            led.value(1)
            j.open_file("pics/tufty-name.png")
            j.decode()
            led.value(0)
            display.update()
            sleep(60)
            sleeptime = 99
        if (button_c.read()):
            # initialize lifegrid
            for w in range(2):
                for i in range(life_x * life_y):
                    lifegrid[w][i] = 0
            # some random dots
            for i in range(initial_dots_num):
                x = random.randint(1, life_x-2)
                y = random.randint(1, life_y-2)
                lifegrid[0][x * life_y + y] = 1

            frames = 0
            fnow = 0
            fnext = 1

            # game loop
            # first frame: draw everything
            display.set_pen(BLACK)
            display.clear()
            for x in range(life_x):
                for y in range(life_y):
                    cell_value = lifegrid[fnow][x * life_y + y]
                    if cell_value == 1:
                        display.set_pen(WHITE)
                        display.rectangle(x*life_size, y*life_size, life_size, life_size)
                    elif cell_value == 2:
                        display.set_pen(RED)
                        display.rectangle(x*life_size, y*life_size, life_size, life_size)
            display.update()

            total_calc = 0
            total_detect = 0
            total_draw = 0
            total_update = 0

            while (frames < life_frames):
                # calculate next generation using optimized function
                t0 = ticks_ms()
                calculate_generation(fnow, fnext)
                t1 = ticks_ms()
                total_calc += ticks_diff(t1, t0)

                # detect changes - mark in viper, collect in native
                mark_changes(fnow, fnext)
                white_cells, red_cells, black_cells = collect_changes(fnext)

                t2 = ticks_ms()
                total_detect += ticks_diff(t2, t1)

                # draw all cells of same color together (minimizes set_pen calls)
                if black_cells:
                    display.set_pen(BLACK)
                    for x, y in black_cells:
                        display.rectangle(x*life_size, y*life_size, life_size, life_size)
                if white_cells:
                    display.set_pen(WHITE)
                    for x, y in white_cells:
                        display.rectangle(x*life_size, y*life_size, life_size, life_size)
                if red_cells:
                    display.set_pen(RED)
                    for x, y in red_cells:
                        display.rectangle(x*life_size, y*life_size, life_size, life_size)

                t3 = ticks_ms()
                total_draw += ticks_diff(t3, t2)

                display.update()

                t4 = ticks_ms()
                total_update += ticks_diff(t4, t3)

                # swap buffers
                fnow, fnext = fnext, fnow
                frames = frames + 1

                # Print timing every 50 frames
                if frames % 50 == 0:
                    print(f"Frame {frames}: calc={total_calc}ms detect={total_detect}ms draw={total_draw}ms update={total_update}ms changed={len(white_cells)+len(red_cells)+len(black_cells)}")
                    total_calc = total_detect = total_draw = total_update = 0

                if (button_c.read()):
                    frames = life_frames
            sleeptime = 99
                       
                        
                        
                        
                        
                        
                        
                        
                        
                        