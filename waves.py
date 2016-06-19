#!/bin/env python
import sys
import serial
import pygame
from threading import Thread
import Queue
import struct

LINE_COLOR = [pygame.Color(255, 0, 0, 255), pygame.Color(0, 0, 255, 255)]
CLEAR_COLOR = pygame.Color(0, 0, 0, 255)
TEXT_COLOR = pygame.Color(255, 255, 255, 255)
MARKER_COLOR = pygame.Color(0, 255, 0, 255)
GUI_BUTTON_LINE_COLOR = pygame.Color(255, 255, 255, 255)
GUI_HOVER_COLOR = pygame.Color(100, 50, 50, 255)
GUI_DOWN_COLOR = pygame.Color(100, 100, 50, 255)
GUI_LINE_WIDTH = 2
GUI_MARGIN = 2
TRANSPARENT_COLOR = pygame.Color(0, 0, 0, 0)
WIDTH = 1000
HEIGHT = 480
N_LINES = 2

INPUT_STRUCT_FORMAT = "=HHHHHffHHHh"
SAMPLE_STRUCT_FORMAT = "=f"
CONTROL_STRUCT_FORMAT = "bbb"

MIDI_MAP = dict((int(line.split(".")[0]) - 1, line.split(".")[1].strip())  for line in open("midi.dat"))

pygame.init()

line_queue = Queue.Queue()
command_queue = Queue.Queue()

gui_sfc = None

gui = {}


def send_command(num, t, value):
    command_queue.put_nowait((num, t, value))


def Button(gid, pos, size, text):
    gui.setdefault(gid, False)
    rect = pygame.Rect(pos[0], pos[1], size[0], size[1])

    hover = rect.collidepoint(pygame.mouse.get_pos()) == 1
    click = pygame.mouse.get_pressed()[0] == 1
    result = False

    bg = CLEAR_COLOR

    if hover and not click:
        if not gui[gid]:
            bg = GUI_HOVER_COLOR
        else:
            result = True

    if hover and click:
        bg = GUI_DOWN_COLOR

    gui[gid] = click and hover

    gui_sfc.fill(
        bg,
        rect)

    pygame.draw.rect(
        gui_sfc,
        GUI_BUTTON_LINE_COLOR,
        rect,
        2)

    name_line = font.render(
        text,
        True,
        TEXT_COLOR,
        bg,
    )
    gui_sfc.blit(name_line, pygame.Rect(
        pos[0] + GUI_LINE_WIDTH + GUI_MARGIN,
        pos[1] + GUI_LINE_WIDTH + GUI_MARGIN,
        size[0] - 2 * (GUI_LINE_WIDTH + GUI_MARGIN),
        size[1] - 2 * (GUI_LINE_WIDTH + GUI_MARGIN)))

    return result


def empty_serial():
    s = serial.Serial(sys.argv[1], 115200)
    data = ""
    struct_size = struct.calcsize(INPUT_STRUCT_FORMAT)
    sample_struct_size = struct.calcsize(SAMPLE_STRUCT_FORMAT)
    waiting_for_samples = 0
    while True:
        if not command_queue.empty():
            s.write(struct.pack(
                CONTROL_STRUCT_FORMAT,
                *command_queue.get_nowait()))

        data = data + s.read(struct_size)
        if waiting_for_samples == 0:
            while len(data) >= struct_size:
                packet = data[:struct_size]
                line = struct.unpack(INPUT_STRUCT_FORMAT, packet)
                if line[0] != 0xFFFF or line[1] != 0x0000 or line[2] != 0xFFFF:
                    print "SKIPPING!"
                    data = data[1:]
                    continue
                else:
                    waiting_for_samples = line[-1]
                    if waiting_for_samples > 200:
                        waiting_for_samples = 0
                        data = data[1:]
                        continue
                    data = data[struct_size:]
                    line_queue.put_nowait(line[3:])
        else:
            while waiting_for_samples > 0 and len(data) >= sample_struct_size:
                packet = data[:sample_struct_size]
                sample = struct.unpack(SAMPLE_STRUCT_FORMAT, packet)[0]
                data = data[sample_struct_size:]
                waiting_for_samples -= 1
                line_queue.put_nowait(sample)

t = Thread(target=empty_serial)
t.daemon = True
t.start()
font = pygame.font.SysFont("mono", 10)
wnd = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Magic Tub Tester")

lines_sfc = pygame.Surface((WIDTH, HEIGHT))
lines_sfc.fill(TRANSPARENT_COLOR)
gui_sfc = pygame.Surface((WIDTH, HEIGHT)).convert_alpha()


def update_graph():
    lines_sfc.fill(CLEAR_COLOR)
    for i in range(N_LINES):
        line = line_queue.get()
        j, mp, mv, alpha, tl, th, inst, steps = line
        step_size = WIDTH / (steps + 1)
        row_height = HEIGHT / N_LINES * (i + 1)
        offset = -step_size
        if i != int(j):
            print "Missed sample!"
            break

        v = 0
        for j in range(steps):
            pv = v
            v = line_queue.get()
            v = int(v / 1024 * 100)

            pygame.draw.line(
                lines_sfc,
                LINE_COLOR[i],
                (offset, row_height - pv),
                (offset + step_size, row_height - v)
            )

            offset += step_size

        name_line = font.render(
            "A%d, TL: %d TH: %d A: %.2f MV: %f MP: %d inst: %s          " % (
                i, tl, th, alpha, mv, mp, MIDI_MAP[inst]),
            True,
            TEXT_COLOR,
            CLEAR_COLOR
        )
        lines_sfc.blit(
            name_line,
            pygame.Rect(10, HEIGHT / N_LINES * i + 60,
                        HEIGHT, WIDTH))

        pygame.draw.circle(
            lines_sfc,
            LINE_COLOR[i],
            (step_size * (mp + 1), row_height - int(mv / 1024 * 100)),
            5)

        pygame.draw.line(
            lines_sfc,
            MARKER_COLOR,
            (tl * step_size, row_height),
            (tl * step_size, row_height - 100)
        )

        pygame.draw.line(
            lines_sfc,
            MARKER_COLOR,
            (th * step_size, row_height),
            (th * step_size, row_height - 100)
        )


def main_loop():
    while True:
        if not line_queue.empty():
            update_graph()

        for i in range(N_LINES):
            h = (HEIGHT / N_LINES) * i
            for (n, t) in enumerate(("TL", "TH", "ALPHA", "BANK", "INST")):
                for vi, v in enumerate((1, -1, 10, -10)):
                    if Button(
                        "%s%d%d" % (t, i, v),
                        ((65 * (vi/2 + (2 * n))), 5 + h + 25 * ((1 + vi % 2)/2)),
                        (60, 20),
                        "%s %+d" % (t, v)
                    ):
                        send_command(i, n, v)

        wnd.blit(lines_sfc, pygame.Rect(0, 0, WIDTH, HEIGHT))
        wnd.blit(gui_sfc, pygame.Rect(0, 0, WIDTH, HEIGHT))
        pygame.display.flip()

        for evt in pygame.event.get():
            if evt.type == pygame.QUIT:
                return

        gui_sfc.fill(TRANSPARENT_COLOR)
        wnd.fill(CLEAR_COLOR)

main_loop()
