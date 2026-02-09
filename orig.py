from picographics import PicoGraphics, DISPLAY_TUFTY_2040
from pimoroni import Button
from time import sleep
from jpegdec import JPEG
from pngdec import PNG
from machine import ADC, Pin, PWM
import math, random, gc
import random

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

life_x = 40
life_y = 30
life_size = 8
life_frames = 500
lifegrid = [[[0 for z in range(life_y)] for y in range(life_x)] for x in range(2)]
initial_dots_num = 1000

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
            for w in range (2):
                for x in range (life_x):
                    for y in range (life_y):
                        lifegrid[w][x][y] = 0
            # some random dots
            for i in range (initial_dots_num):
                x = random.randint(1,life_x-2)
                y = random.randint(1,life_y-2)
                lifegrid[0][x][y] = 1

            frames = 0
            fnow = 0
            fnext = 1
            # game loop
            while (frames < life_frames):
                # draw screen
                display.set_pen(BLACK)
                display.clear()
                
                for x in range (life_x):
                    for y in range (life_y):
                        if (lifegrid[fnow][x][y] == 1):
                            display.set_pen(WHITE)
                            display.rectangle(x*life_size, y*life_size, life_size, life_size)
                        if (lifegrid[fnow][x][y] == 2):
                            display.set_pen(RED)
                            display.rectangle(x*life_size, y*life_size, life_size, life_size)
                display.update()
                # clear the second grid
                #for x in range (life_x):
                #    for y in range (life_y):
                #        lifegrid[1][x][y] = 0
                # calculate live / dead
                for x in range (1,life_x-1):
                    for y in range (1,life_y-1):
                        # count neighbors
                        neighbors = 0
                        if (lifegrid[fnow][x-1][y] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x+1][y] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x][y-1] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x][y+1] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x-1][y-1] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x-1][y+1] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x+1][y+1] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x+1][y-1] == 1):
                            neighbors = neighbors + 1
                        if (lifegrid[fnow][x][y] == 1):   # alive
                            if (neighbors < 2 or neighbors > 3):
                                lifegrid[fnext][x][y] = 2	# died
                            elif (2 <= neighbors <= 3):
                                lifegrid[fnext][x][y] = 1	# kept alive
                        else:	#dead
                            if neighbors == 3:
                                lifegrid[fnext][x][y] = 1	# dead now alive
                            else:
                                lifegrid[fnext][x][y] = 0	# still dead
                #lifegrid[0] = lifegrid[1]
                if fnow == 0:
                    fnow = 1
                    fnext = 0
                else:
                    fnow = 0
                    fnext = 1
                frames = frames + 1
                if (button_c.read()):
                    frames = life_frames
            sleeptime = 99
                       
                        
                        
                        
                        
                        
                        
                        
                        
                        