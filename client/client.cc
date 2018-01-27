#include "sio_client.h"
#include "sio_socket.h"

#include <unistd.h>

#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string>
#include <thread>
#include <thread>

struct Pixel {
  int identity;
  std::vector<int> r;
  std::vector<int> g;
  std::vector<int> b;
};

struct Routine {
  int identity;
  int cycles;
  double interval;
  std::vector<Pixel> pixels;
};

std::mutex routines_mutex;
std::vector<Routine> routines, routines_buffer;
sio::client client;
bool play = false;
int routine_cursor = 0;
int frame_cursor = 0;

void on_connect() {
  client.socket()->on(
      "sign receive routine from server",
      sio::socket::event_listener_aux(
          [&](std::string const& name, sio::message::ptr const& data,
              bool isAck, sio::message::list& ack_resp) {

            int identity = data->get_map()["identity"]->get_int();
            int cycles = data->get_map()["cycles"]->get_int();
            double interval = data->get_map()["interval"]->get_double();
            std::cout << "GOT ROUTINE!! " << identity << ".." << cycles << ".."
                      << interval << "\n";

            std::vector<Pixel> pixels;
            struct Routine routine = {identity, cycles, interval, pixels};

            routines_buffer.push_back(routine);
          }));

  client.socket()->on(
      "sign receive pixel from server",
      sio::socket::event_listener_aux(
          [&](std::string const& name, sio::message::ptr const& data,
              bool isAck, sio::message::list& ack_resp) {
            int routine = data->get_map()["routine"]->get_int();
            int identity = data->get_map()["identity"]->get_int();
            std::vector<int> r, g, b;

            std::vector<std::shared_ptr<sio::message>> r_ptr =
                data->get_map()["r"]->get_vector();
            for (size_t i = 0; i < r_ptr.size(); i++) {
              r.push_back(r_ptr[i]->get_int());
            }

            std::vector<std::shared_ptr<sio::message>> g_ptr =
                data->get_map()["g"]->get_vector();
            for (size_t i = 0; i < g_ptr.size(); i++) {
              g.push_back(g_ptr[i]->get_int());
            }

            std::vector<std::shared_ptr<sio::message>> b_ptr =
                data->get_map()["b"]->get_vector();
            for (size_t i = 0; i < b_ptr.size(); i++) {
              b.push_back(b_ptr[i]->get_int());
            }

            struct Pixel pixel = {identity, r, g, b};

            for (size_t i = 0; i < routines_buffer.size(); i++) {
              if (routines_buffer[i].identity == routine) {
                routines_buffer[i].pixels.push_back(pixel);
              }
            }
          }));

  client.socket()->on(
      "apply to sign",
      sio::socket::event_listener_aux(
          [&](std::string const& name, sio::message::ptr const& data,
              bool isAck, sio::message::list& ack_resp) {
            if (routines_buffer.size() < 1) {
              std::cout << "Did not receive the required number of routines ("
                        << routines_buffer.size() << ")\n";
              return;
            }

            if (routines_buffer[0].pixels.size() < 150) {
              std::cout << "Did not receive the required number of pixels ("
                        << routines_buffer[0].pixels.size() << "\n";
              return;
            }

            play = false;
            routines_mutex.lock();
            routines = routines_buffer;
            frame_cursor = 0;
            routine_cursor = 0;
            routines_mutex.unlock();
            play = true;
          }));

  client.socket()->emit("send to sign");
}

void on_close(sio::client::close_reason const& reason) {
  std::cout << "closed " << std::endl;
}

void on_fail() { std::cout << "failed " << std::endl; }

void play_lights() {
  while (true) {
    if (!play) {
      usleep(1e6 * 0.01);
      continue;
    }

    routines_mutex.lock();
    std::vector<Routine> routines_tmp = routines;
    routines_mutex.unlock();

    for (size_t current_routine = 0; current_routine < routines.size() && play;
         current_routine++) {
      int cycles = routines_tmp[current_routine].cycles;
      double interval = routines_tmp[current_routine].interval;

      for (size_t current_cycle = 0; current_cycle < (size_t)cycles;
           current_cycle++) {
        for (size_t current_frame = 0;
             current_frame < routines_tmp[current_routine].pixels[0].r.size();
             current_frame++) {
          for (size_t current_pixel = 0; current_pixel < 150; current_pixel++) {
          }

          usleep(1e6 * interval);
        }
      }
    }
  }
}

int main() {
  // TODO: sudo pip install eventlet gevent-websocket

  std::thread lights_player(play_lights);

  client.set_open_listener(on_connect);
  client.set_close_listener(on_close);
  client.set_fail_listener(on_fail);
  client.connect("http://127.0.0.1:5000/");

  lights_player.join();
}
