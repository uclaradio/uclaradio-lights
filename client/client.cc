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

ws2811_led_t leds[300];

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
bool play = true;
bool reset = false;

bool disable_lights = false;

ws2811_led_t rgb_to_led_color(unsigned char red, unsigned char green,
                              unsigned char blue) {
  unsigned char white = 0;

  return (white << 24) | (blue << 16) | (green << 8) | red;
}

void on_connect() {
  std::cout << "CONNECTED!" << std::endl;

  client.socket()->on(
      "toggle lights sign",
      sio::socket::event_listener_aux([&](
          std::string const& name, sio::message::ptr const& data, bool isAck,
          sio::message::list& ack_resp) { disable_lights = !disable_lights; }));
}

void on_close(sio::client::close_reason const& reason) {
  std::cout << "closed " << std::endl;
}

void on_fail() { std::cout << "failed " << std::endl; }

char mix(char one, char two, float slept, float interval) {
  float mix = slept / interval;
  return one * (1 - mix) + two * (mix);
}

typedef struct {
  double r;  // a fraction between 0 and 1
  double g;  // a fraction between 0 and 1
  double b;  // a fraction between 0 and 1
} rgb;

typedef struct {
  double h;  // angle in degrees
  double s;  // a fraction between 0 and 1
  double v;  // a fraction between 0 and 1
} hsv;

static rgb hsv2rgb(hsv in);

rgb hsv2rgb(hsv in) {
  double hh, p, q, t, ff;
  long i;
  rgb out;

  if (in.s <= 0.0) {  // < is bogus, just shuts up warnings
    out.r = in.v;
    out.g = in.v;
    out.b = in.v;
    return out;
  }
  hh = in.h;
  if (hh >= 360.0) hh = 0.0;
  hh /= 60.0;
  i = (long)hh;
  ff = hh - i;
  p = in.v * (1.0 - in.s);
  q = in.v * (1.0 - (in.s * ff));
  t = in.v * (1.0 - (in.s * (1.0 - ff)));

  switch (i) {
    case 0:
      out.r = in.v;
      out.g = t;
      out.b = p;
      break;
    case 1:
      out.r = q;
      out.g = in.v;
      out.b = p;
      break;
    case 2:
      out.r = p;
      out.g = in.v;
      out.b = t;
      break;

    case 3:
      out.r = p;
      out.g = q;
      out.b = in.v;
      break;
    case 4:
      out.r = t;
      out.g = p;
      out.b = in.v;
      break;
    case 5:
    default:
      out.r = in.v;
      out.g = p;
      out.b = q;
      break;
  }
  return out;
}

class Blank {
 public:
  Blank(int num_leds) : num_leds_(num_leds) {}

  void RunIteration(ws2811_t& leds) {
    for (int i = 0; i < num_leds_; i++) {
      leds.channel[0].leds[i] = rgb_to_led_color(0, 0, 0);
    }
  }

 private:
  int num_leds_;
};

class Sweep {
 public:
  Sweep(int num_leds) : frame_(0), num_leds_(num_leds), r_(0), g_(0), b_(0) {}

  void RunIteration(ws2811_t& leds) {
    if (frame_ == 0) {
      rgb color = hsv2rgb({static_cast<double>(rand() % 360), 1, 1});
      r_ = color.r * 255;
      g_ = color.g * 255;
      b_ = color.b * 255;
    }

    for (int i = 0; i < num_leds_; i++) {
      if (frame_ < num_leds_) {
        if (i < frame_) {
          leds.channel[0].leds[i] = rgb_to_led_color(r_, g_, b_);
        } else {
          leds.channel[0].leds[i] = rgb_to_led_color(0, 0, 0);
        }
      } else {
        if (i > frame_ - num_leds_) {
          leds.channel[0].leds[i] = rgb_to_led_color(r_, g_, b_);
        } else {
          leds.channel[0].leds[i] = rgb_to_led_color(0, 0, 0);
        }
      }
    }

    if (frame_++ > num_leds_ * 2) {
      frame_ = 0;
    }
  }

 private:
  int frame_;
  int num_leds_;
  int r_, g_, b_;
};

class Pong {
 public:
  Pong(int num_leds)
      : frame_(0),
        spread_(0),
        num_leds_(num_leds),
        r_(0),
        g_(0),
        b_(0),
        reset_(true) {}

  Pong(int num_leds, int frame, int spread, int r, int g, int b)
      : frame_(frame),
        spread_(spread),
        num_leds_(num_leds),
        r_(r),
        g_(g),
        b_(b),
        reset_(false) {}

  void RunIteration(ws2811_t& leds) {
    if (frame_ == 0) {
      if (reset_) {
        rgb color = hsv2rgb({static_cast<double>(rand() % 360), 1, 1});
        r_ = color.r * 255;
        g_ = color.g * 255;
        b_ = color.b * 255;

        spread_ = rand() % 50 + 10;
      }
    }

    int bottom = frame_;

    if (frame_ > num_leds_) {
      bottom = 600 - frame_;
    }

    for (double i = 0; i < num_leds_; i++) {
      if (i > bottom && i < bottom + spread_) {
        leds.channel[0].leds[(int)i] = rgb_to_led_color(r_, g_, b_);
      } else {
        if (reset_) {
          leds.channel[0].leds[(int)i] = rgb_to_led_color(0, 0, 0);
        }
      }
    }

    frame_++;

    if (frame_ > num_leds_ * 2 + spread_) {
      frame_ = 0;
    }
  }

 private:
  int frame_;
  int spread_;
  int num_leds_;
  int r_, g_, b_;
  bool reset_;
};

class MultiPong {
 public:
  MultiPong(int num_leds)
      : frame_(0), spread_(0), num_leds_(num_leds), r_(0), g_(0), b_(0) {
    for (int i = 0; i < 30; i++) {
      rgb color = hsv2rgb({static_cast<double>(rand() % 360), 1, 1});
      int r = color.r * 255;
      int g = color.g * 255;
      int b = color.b * 255;
      pong[i] = new Pong(num_leds, rand() % (num_leds * 2 - 80),
                         rand() % 40 + 1, r, g, b);
    }
  }

  ~MultiPong() {
    for (int i = 0; i < 10; i++) {
      delete pong[i];
    }
  }

  void RunIteration(ws2811_t& leds) {
    for (int i = 0; i < num_leds_; i++) {
      leds.channel[0].leds[i] = rgb_to_led_color(0, 0, 0);
    }

    for (int i = 0; i < 30; i++) {
      pong[i]->RunIteration(leds);
    }
  }

 private:
  Pong* pong[30];
  int frame_;
  int spread_;
  int num_leds_;
  int r_, g_, b_;
};

class ColorBand {
 public:
  ColorBand(int num_leds) : frame_(0), num_leds_(num_leds) {}

  void RunIteration(ws2811_t& leds) {
    rgb color = hsv2rgb({(double)frame_ / 2, 1, 1});
    int r = color.r * 255;
    int g = color.g * 255;
    int b = color.b * 255;

    for (int i = 0; i < num_leds_; i++) {
      leds.channel[0].leds[i] = rgb_to_led_color(r, g, b);
    }

    if (frame_++ > 720) {
      frame_ = 0;
    }
  }

 private:
  int frame_;
  int num_leds_;
  int r_, g_, b_;
};

class Space {
 public:
  Space(int num_leds) : num_leds_(num_leds) {
    leds_ = new int[num_leds_];
    for (int i = 0; i < num_leds_; i++) {
      leds[i] = 0;
    }
  }

  ~Space() { delete[] leds_; }

  void RunIteration(ws2811_t& leds) {
    int new_star = rand() % num_leds_;
    if (leds_[new_star] == 0 && pow(rand() % 4, 2) > 7) {
      leds_[new_star] = 1;
    }

    for (int i = 0; i < num_leds_; i++) {
      if (leds_[i] > 0) {
        leds_[i] += rand() % 8 + 0;
        if (leds_[i] >= 255 * 2) {
          leds_[i] = 0;
        }
      }
      if (leds_[i] > 255) {
        leds.channel[0].leds[i] = rgb_to_led_color(0, 0, 255 * 2 - leds_[i]);
      } else {
        leds.channel[0].leds[i] = rgb_to_led_color(0, 0, leds_[i]);
      }
    }
  }

 private:
  int* leds_;
  int num_leds_;
};

class Rainbow {
 public:
  Rainbow(int num_leds) : num_leds_(num_leds), frame_(0) {
    leds_r_ = new int[num_leds_];
    leds_g_ = new int[num_leds_];
    leds_b_ = new int[num_leds_];

    for (int i = 0; i < num_leds_; i++) {
      leds_r_[i] = 0;
      leds_g_[i] = 0;
      leds_b_[i] = 0;
    }
  }

  ~Rainbow() {
    delete[] leds_r_;
    delete[] leds_g_;
    delete[] leds_b_;
  }

  void RunIteration(ws2811_t& leds) {
    rgb color = hsv2rgb({(double)frame_, 1, 1});
    leds_r_[0] = color.r * 255;
    leds_g_[0] = color.g * 255;
    leds_b_[0] = color.b * 255;

    for (int i = num_leds_; i > 0; i--) {
      leds_r_[i] = leds_r_[i - 1];
      leds_g_[i] = leds_g_[i - 1];
      leds_b_[i] = leds_b_[i - 1];

      leds.channel[0].leds[i] =
          rgb_to_led_color(leds_r_[i], leds_g_[i], leds_b_[i]);
    }

    if (frame_++ >= 360) {
      frame_ = 0;
    }
  }

 private:
  int* leds_r_;
  int* leds_g_;
  int* leds_b_;
  int num_leds_;
  int frame_;
};

void play_lights() {
  std::cout << "PLAYING LIGHTS!" << std::endl;
  ws2811_return_t ret;

  // Initialize routine classes.
  int selected_routine = rand() % 6;
  Blank blank(LED_COUNT);
  Sweep sweep(LED_COUNT);
  Pong pong(LED_COUNT);
  MultiPong multi_pong(LED_COUNT);
  ColorBand color_band(LED_COUNT);
  Space space(LED_COUNT);
  Rainbow rainbow(LED_COUNT);

  if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
    fprintf(stderr, "ws2811_init failed\n");
    return;
  }

  while (true) {
    reset = false;

    routines_mutex.lock();
    std::vector<Routine> routines_tmp = routines;
    routines_mutex.unlock();
    //  const float brightness = 1.0;
    const double mini_sleep = 0.10;
    double slept = 0;
    double interval = 1 / 30;

    while (!reset && play) {
      switch(disable_lights ? -1 : selected_routine) {
        case -1:
          blank.RunIteration(ledstring);
          break;
        case 0:
          sweep.RunIteration(ledstring);
          break;
        case 1:
          pong.RunIteration(ledstring);
          break;
        case 2:
          multi_pong.RunIteration(ledstring);
          break;
        case 3:
          color_band.RunIteration(ledstring);
          break;
        case 4:
          space.RunIteration(ledstring);
          break;
        case 5:
          rainbow.RunIteration(ledstring);
          break;
      }

      if(rand() % (1000000) < 100) {
        selected_routine = rand() % 6;
      }

      if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
        fprintf(stderr, "ws2811_render failed\n");
        break;
      }

      while (slept < interval && play && !reset) {
        slept += mini_sleep;
        usleep(mini_sleep * 1e6);
      }
    }
  }

  ::std::cout << "END\n";

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
