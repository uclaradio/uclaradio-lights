from flask import Flask, render_template
from flask_socketio import SocketIO, emit

import json
import math

app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app)

current_lights = None
lights = None
routines = dict()
routines_buffer = dict()

class Light:
    def __init__(self, identity, r, g, b, x, y):
        self.identity = identity
        self.r = r
        self.g = g
        self.b = b
        self.x = x
        self.y = y

@socketio.on('connect')
def connect():
    print "someone connected"
    socketio.emit('sign receive from server', 'test')

    if current_lights is not None:
        for light in current_lights:
            socketio.emit('get lights', light)
            print "sending..."

@socketio.on('send to sign')
def send_to_sign():
    global routines

    for routine in routines.keys():
        new_routine = routines[routine].copy()
        new_routine['pixels'] = dict()
        socketio.emit("sign receive routine from server", new_routine)
        print "emit routine " + str(new_routine)

        for pixel in routines[routine]['pixels']:
            pixel_copy = routines[routine]['pixels'][pixel].copy()
            pixel_copy['identity'] = pixel
            pixel_copy['routine'] = routine
            print pixel_copy
            socketio.emit("sign receive pixel from server", \
                    pixel_copy)

    socketio.emit("apply to sign")

@socketio.on('set lights')
def set_lights(message):
    global lights

    lights_json = message
    identity = int(lights_json['identity'])
    if identity < 0 or identity > len(lights):
        print "ERROR: Light to be set is out of range."
        return

    if lights_json.has_key('r') and \
       lights_json.has_key('g') and \
       lights_json.has_key('b'):
        lights[identity].r = lights_json['r']
        lights[identity].g = lights_json['g']
        lights[identity].b = lights_json['b']

    if lights_json.has_key('x') and \
       lights_json.has_key('y'):
        lights[identity].x = lights_json['x']
        lights[identity].y = lights_json['y']

@socketio.on('get lights server')
def get_lights():
    global socketio
    global lights
    for light in lights:
        data = {}
        data['identity'] = light.identity
        data['r'] = light.r
        data['g'] = light.g
        data['b'] = light.b
        data['x'] = light.x
        data['y'] = light.y
        socketio.emit("get lights client", data)
    socketio.emit("get lights finish");

@socketio.on('reset routines')
def upload_routines():
    global routines_buffer

    routines_buffer = dict()

@socketio.on('create routine')
def create_routine(routine):
    global routines_buffer

    print "create routine " + str(routine)
    routines_buffer[routine['identity']] = routine

@socketio.on('upload pixel')
def upload_pixel(pixel):
    global routines_buffer

    print "upload pixel " + str(pixel)
    routines_buffer[pixel['routine']]['pixels'][pixel['identity']] = \
            pixel['pixel_routine'];

@socketio.on('finish upload')
def finish_upload():
    global routines
    global routines_buffer

    should_upload = True

    for routine in routines_buffer:
        if len(routines_buffer[routine]['pixels']) != 150:
            should_upload = False
            break

    if should_upload:
        routines = routines_buffer.copy()
    else:
        print "ERROR: Missing pixels."

@socketio.on('download routine')
def download_routine():
    socketio.emit('start download')

    for routine in routines.keys():
        new_routine = routines[routine].copy()
        new_routine['pixels'] = dict()
        socketio.emit("create routine client", new_routine)

        for pixel in routines[routine]['pixels']:
            pixel_copy = routines[routine]['pixels'][pixel].copy()
            pixel_copy['identity'] = pixel
            pixel_copy['routine'] = routine
            socketio.emit("download pixel client", \
                    pixel_copy)

    socketio.emit('finish download')

@socketio.on('load lights')
def load_lights():
    if current_lights is not None:
        for light in current_lights:
            socketio.emit('send lights', light)

if __name__ == '__main__':
    lights = list()
    for i in range(0, 150):
        r = 0
        g = 0
        b = 0
        x = (i % 10) * 25
        y = math.floor(i / 10) * 25
        lights.append(Light(i, r, g, b, x, y))

    socketio.run(app)
