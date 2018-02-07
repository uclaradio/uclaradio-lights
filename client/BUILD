package(default_visibility = ['//visibility:public'])

cc_binary(
  name = 'uclaradio-lights_client',
  visibility = ['//visibility:public'],
  srcs = [
    'client.cc',
  ],
  deps = [
    "@rpi_ws281x//:rpi_ws281x",
    "@socketio_client_cpp//:socketio_client_cpp",
  ],
  copts = [
    '-std=c++11',
    '-pipe',
    '-Wno-unused-parameter',
  ],
)
