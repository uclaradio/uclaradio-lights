#include "sio_client.h"
#include "sio_socket.h"

#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string>
#include <thread>
#include <thread>

#include "clk.h"
#include "dma.h"
#include "gpio.h"
#include "pwm.h"
#include "version.h"

#include "ws2811.h"

#define TARGET_FREQ 800000
#define GPIO_PIN 18
#define DMA 5
#define STRIP_TYPE WS2811_STRIP_GBR
#define LED_COUNT 300

extern "C" {
ws2811_return_t ws2811_init(ws2811_t* ws2811);
}

ws2811_led_t leds[170];

ws2811_channel_t channel_0 = {GPIO_PIN, 0, LED_COUNT, STRIP_TYPE, leds,   255,
                              0,        0, 0,         0,          nullptr};

ws2811_channel_t channel_1 = {GPIO_PIN, 0, LED_COUNT, STRIP_TYPE, nullptr, 255,
                              0,        0, 0,         0,          nullptr};

ws2811_t ledstring = {0,           nullptr, nullptr,
                      TARGET_FREQ, DMA,     {channel_0, channel_1}};

struct Pixel {
  int identity;
  std::vector<unsigned char> r;
  std::vector<unsigned char> g;
  std::vector<unsigned char> b;
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
bool reset = true;

ws2811_led_t rgb_to_led_color(unsigned char red, unsigned char green,
                              unsigned char blue) {
  unsigned char white = 0;

  return (white << 24) | (blue << 16) | (green << 8) | red;
}

void on_connect() {
  std::cout << "CONNECTED!" << std::endl;
  client.socket()->on(
      "sign reset buffer",
      sio::socket::event_listener_aux(
          [&](std::string const& name, sio::message::ptr const& data,
              bool isAck, sio::message::list& ack_resp) {

            routines_buffer = std::vector<Routine>();
          }));

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
            std::vector<unsigned char> r, g, b;

            std::vector<std::shared_ptr<sio::message>> r_ptr =
                data->get_map()["r"]->get_vector();
            for (size_t i = 0; i < r_ptr.size(); i++) {
              r.push_back(static_cast<unsigned char>(r_ptr[i]->get_int()));
            }

            std::vector<std::shared_ptr<sio::message>> g_ptr =
                data->get_map()["g"]->get_vector();
            for (size_t i = 0; i < g_ptr.size(); i++) {
              g.push_back(static_cast<unsigned char>(g_ptr[i]->get_int()));
            }

            std::vector<std::shared_ptr<sio::message>> b_ptr =
                data->get_map()["b"]->get_vector();
            for (size_t i = 0; i < b_ptr.size(); i++) {
              b.push_back(static_cast<unsigned char>(b_ptr[i]->get_int()));
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
            routines_mutex.unlock();
            play = true;
            reset = true;
          }));

  client.socket()->emit("send to sign");
}

void on_close(sio::client::close_reason const& reason) {
  std::cout << "closed " << std::endl;
}

void on_fail() { std::cout << "failed " << std::endl; }

char mix(char one, char two, float slept, float interval) {
  float mix = slept / interval;
  return one * (1 - mix) + two * (mix);
}

void play_lights() {
  std::cout << "PLAYING LIGHTS!" << std::endl;
  ws2811_return_t ret;

  if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
    fprintf(stderr, "ws2811_init failed\n");
    return;
  }

  int i = 0;
  while (true) {
    if (!play) {
      if (reset) {
        i++;
        if (i > 10000) i = 0;

        for (size_t current_pixel = 0; current_pixel < 300; current_pixel++) {
          char r = 10 + (current_pixel * 26 + i * 24) % 20;
          char g = 0 + (current_pixel * 15 + i * 10) % 1;
          char b = 0 + (current_pixel * 4 + i * 5) % 1;

          ledstring.channel[0].leds[current_pixel] = rgb_to_led_color(r, g, b);
        }

        if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
          fprintf(stderr, "ws2811_render failed\n");
          break;
        }
      }

      usleep(1e6 * 0.1);
      continue;
    }

    std::cout << "RESET\n";

    reset = false;

    routines_mutex.lock();
    std::vector<Routine> routines_tmp = routines;
    routines_mutex.unlock();
    const float brightness = 0.7;

    while (!reset && play) {
      for (size_t current_routine = 0;
           current_routine < routines.size() && play && !reset;
           current_routine++) {
        int cycles = routines_tmp[current_routine].cycles;
        double interval = routines_tmp[current_routine].interval;

        for (size_t current_cycle = 0;
             current_cycle < (size_t)cycles && play && !reset;
             current_cycle++) {
          for (size_t current_frame = 0;
               current_frame <
                   routines_tmp[current_routine].pixels[0].r.size() &&
               play && !reset;
               current_frame++) {
            std::cout << "running routine " << current_routine << " cycle "
                      << current_cycle << " frame " << current_frame
                      << std::endl;

            double slept = 0;
            const double mini_sleep = 0.10;
            const int spread = 40;

            while (slept < interval && play && !reset) {
              i++;
              for (size_t current_pixel = 0;
                   current_pixel < 150 && play && !reset; current_pixel++) {
                if (i > 1000000) i = 0;

                int next_frame =
                    (current_frame + 1) %
                    routines_tmp[current_routine].pixels[0].r.size();
                char r = mix(routines_tmp[current_routine]
                                 .pixels[current_pixel]
                                 .r[current_frame],
                             routines_tmp[current_routine]
                                 .pixels[current_pixel]
                                 .r[next_frame],
                             slept, interval);
                char g = mix(routines_tmp[current_routine]
                                 .pixels[current_pixel]
                                 .g[current_frame],
                             routines_tmp[current_routine]
                                 .pixels[current_pixel]
                                 .g[next_frame],
                             slept, interval);
                char b = mix(routines_tmp[current_routine]
                                 .pixels[current_pixel]
                                 .b[current_frame],
                             routines_tmp[current_routine]
                                 .pixels[current_pixel]
                                 .b[next_frame],
                             slept, interval);

                for (size_t pair = 0; pair < 2; pair++) {
                  ledstring.channel[0]
                      .leds[current_pixel * 2 + pair] = rgb_to_led_color(
                      std::min(255, std::max(0, (int)(r * ((current_pixel + i) %
                                                           spread) /
                                                      spread))) *
                          brightness,
                      std::min(255, std::max(0, (int)(g * ((current_pixel + i) %
                                                           spread) /
                                                      spread))) *
                          brightness,
                      std::min(255, std::max(0, (int)(b * ((current_pixel + i) %
                                                           spread) /
                                                      spread))) *
                          brightness);
                }
              }

              if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
                fprintf(stderr, "ws2811_render failed\n");
                break;
              }

              slept += mini_sleep;
              usleep(mini_sleep * 1e6);
            }
          }
        }
      }
    }
  }

  ws2811_fini(&ledstring);
}

int main() {
  std::thread lights_player(play_lights);

  client.set_open_listener(on_connect);
  client.set_close_listener(on_close);
  client.set_fail_listener(on_fail);
  client.connect("http://99.113.35.3:443/");

  lights_player.join();
}
