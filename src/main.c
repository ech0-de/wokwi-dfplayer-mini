// DFPlayer Mini devboard simulation for Wokwi custom chips.
// Datasheet: https://picaxe.com/docs/spe033.pdf
//
// Implements the DFPlayer Mini UART command protocol (10-byte frames:
// 7E VER LEN CMD FEEDBACK PARA1 PARA2 CSUM_H CSUM_L EF) closely enough
// to drive real host libraries (e.g. DFRobotDFPlayerMini): playback
// control, volume/EQ/mode, source selection, status queries, the
// power-on/reset "device online" announcement, and BUSY-pin behavior.
// Since there is no audio backend in the simulator, tracks are timed
// out after a configurable duration instead of actually decoding MP3s,
// and DAC/SPK/USB pins are exposed for footprint accuracy only.

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_LEN 10
#define START_BYTE 0x7E
#define END_BYTE 0xEF
#define VERSION_BYTE 0xFF

#define LONG_PRESS_NS ((uint64_t)700 * 1000 * 1000)

typedef enum {
  STATE_STOPPED = 0,
  STATE_PLAYING = 1,
  STATE_PAUSED = 2,
} playback_state_t;

typedef struct {
  pin_t pin_busy;
  pin_t pin_io1;
  pin_t pin_io2;

  uart_dev_t uart;
  timer_t track_timer;
  timer_t boot_timer;

  uint8_t rx_buf[FRAME_LEN];

  uint32_t track_count_attr;
  uint32_t track_duration_attr;

  uint8_t volume;
  uint8_t eq;
  uint8_t playback_mode;
  uint8_t source;
  bool sleeping;

  playback_state_t state;
  uint16_t current_track;

  uint64_t io1_press_ns;
  uint64_t io2_press_ns;
} chip_state_t;

static uint16_t calc_checksum(uint8_t ver, uint8_t len, uint8_t cmd, uint8_t fb, uint8_t p1, uint8_t p2) {
  uint16_t sum = (uint16_t)(ver + len + cmd + fb + p1 + p2);
  return (uint16_t)(0 - sum);
}

static void send_frame(chip_state_t *chip, uint8_t cmd, uint8_t p1, uint8_t p2) {
  uint8_t buf[FRAME_LEN];
  buf[0] = START_BYTE;
  buf[1] = VERSION_BYTE;
  buf[2] = 0x06;
  buf[3] = cmd;
  buf[4] = 0x00;
  buf[5] = p1;
  buf[6] = p2;
  uint16_t cs = calc_checksum(buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
  buf[7] = (uint8_t)(cs >> 8);
  buf[8] = (uint8_t)(cs & 0xFF);
  buf[9] = END_BYTE;
  uart_write(chip->uart, buf, FRAME_LEN);
}

static uint32_t track_count(chip_state_t *chip) {
  uint32_t count = attr_read(chip->track_count_attr);
  return count == 0 ? 1 : count;
}

static void set_busy_pin(chip_state_t *chip) {
  // BUSY is driven low while a track is playing, high otherwise.
  pin_write(chip->pin_busy, chip->state == STATE_PLAYING ? LOW : HIGH);
}

static void start_playing(chip_state_t *chip) {
  uint32_t count = track_count(chip);
  chip->current_track = (uint16_t)(chip->current_track % count);
  chip->state = STATE_PLAYING;
  set_busy_pin(chip);

  uint32_t duration_ms = attr_read(chip->track_duration_attr);
  if (duration_ms == 0) duration_ms = 3000;
  timer_start(chip->track_timer, duration_ms * 1000, false);
}

static void pause_playing(chip_state_t *chip) {
  timer_stop(chip->track_timer);
  chip->state = STATE_PAUSED;
  set_busy_pin(chip);
}

static void stop_playing(chip_state_t *chip) {
  timer_stop(chip->track_timer);
  chip->state = STATE_STOPPED;
  set_busy_pin(chip);
}

static void on_track_timer(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  // "Finished playing" notification. 0x3D == TF card source.
  send_frame(chip, 0x3D, (uint8_t)(chip->current_track >> 8), (uint8_t)(chip->current_track & 0xFF));

  if (chip->playback_mode == 2) {
    // Single-track repeat: keep playing the same track.
    start_playing(chip);
    return;
  }

  uint32_t count = track_count(chip);
  chip->current_track = (uint16_t)((chip->current_track + 1) % count);

  if (chip->playback_mode == 0 || chip->playback_mode == 1 || chip->playback_mode == 3) {
    // Repeat-all / folder-repeat / random: keep the music going.
    start_playing(chip);
  } else {
    chip->state = STATE_PAUSED;
    set_busy_pin(chip);
  }
}

static void send_online_announcement(chip_state_t *chip) {
  // 0x3F: device-online announcement, sent after power-on and after reset.
  send_frame(chip, 0x3F, 0x00, chip->source);
}

static void on_boot_timer(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  send_online_announcement(chip);
}

static void reset_state(chip_state_t *chip) {
  stop_playing(chip);
  chip->volume = 30;
  chip->eq = 0;
  chip->playback_mode = 0;
  chip->source = 0x02; // TF card
  chip->sleeping = false;
  chip->current_track = 0;
  timer_start(chip->boot_timer, 200 * 1000, false);
}

static void handle_command(chip_state_t *chip, uint8_t cmd, uint8_t feedback, uint8_t p1, uint8_t p2) {
  uint16_t param = (uint16_t)((p1 << 8) | p2);
  bool handled = true;

  switch (cmd) {
    case 0x01: // Next
      chip->current_track = (uint16_t)((chip->current_track + 1) % track_count(chip));
      start_playing(chip);
      break;

    case 0x02: // Previous
      chip->current_track = (uint16_t)((chip->current_track + track_count(chip) - 1) % track_count(chip));
      start_playing(chip);
      break;

    case 0x03: // Specify track (0-2999)
      chip->current_track = param;
      start_playing(chip);
      break;

    case 0x04: // Volume +
      if (chip->volume < 30) chip->volume++;
      break;

    case 0x05: // Volume -
      if (chip->volume > 0) chip->volume--;
      break;

    case 0x06: // Specify volume (0-30)
      chip->volume = p2 > 30 ? 30 : p2;
      break;

    case 0x07: // Specify EQ (0-5)
      chip->eq = p2;
      break;

    case 0x08: // Specify playback mode (0-3)
      chip->playback_mode = p2;
      break;

    case 0x09: // Specify playback source (1=U, 2=TF, 4=SLEEP, ...)
      chip->source = p2;
      break;

    case 0x0A: // Standby / low power
      chip->sleeping = true;
      stop_playing(chip);
      break;

    case 0x0B: // Normal working
      chip->sleeping = false;
      break;

    case 0x0C: // Reset module
      reset_state(chip);
      break;

    case 0x0D: // Playback (resume)
      start_playing(chip);
      break;

    case 0x0E: // Pause
      pause_playing(chip);
      break;

    case 0x0F: // Specify folder(p1)/track(p2) to play
      chip->current_track = param;
      start_playing(chip);
      break;

    case 0x10: // Volume adjust set - accepted, gain not modeled
      break;

    case 0x11: // Repeat play on/off
      chip->playback_mode = p2 ? 0 : chip->playback_mode;
      break;

    case 0x42: // Query current status
      send_frame(chip, 0x42, chip->source, (uint8_t)chip->state);
      break;

    case 0x43: // Query current volume
      send_frame(chip, 0x43, 0x00, chip->volume);
      break;

    case 0x44: // Query current EQ
      send_frame(chip, 0x44, 0x00, chip->eq);
      break;

    case 0x45: // Query current playback mode
      send_frame(chip, 0x45, 0x00, chip->playback_mode);
      break;

    case 0x46: // Query software version
      send_frame(chip, 0x46, 0x01, 0x00);
      break;

    case 0x47: { // Query total TF card files
      uint32_t count = track_count(chip);
      send_frame(chip, 0x47, (uint8_t)(count >> 8), (uint8_t)(count & 0xFF));
      break;
    }

    case 0x48: // Query total U-disk files
      send_frame(chip, 0x48, 0x00, 0x00);
      break;

    case 0x49: // Query total flash files
      send_frame(chip, 0x49, 0x00, 0x00);
      break;

    case 0x4A: // Keep on
      break;

    case 0x4B: // Query current TF card track
      send_frame(chip, 0x4B, (uint8_t)(chip->current_track >> 8), (uint8_t)(chip->current_track & 0xFF));
      break;

    case 0x4C: // Query current U-disk track
      send_frame(chip, 0x4C, 0x00, 0x00);
      break;

    case 0x4D: // Query current flash track
      send_frame(chip, 0x4D, 0x00, 0x00);
      break;

    default:
      handled = false;
      break;
  }

  if (handled && feedback == 0x01) {
    send_frame(chip, 0x41, 0x00, 0x00); // Reply / ACK
  }
}

static void process_frame(chip_state_t *chip) {
  uint8_t *b = chip->rx_buf;
  uint8_t ver = b[1], len = b[2], cmd = b[3], fb = b[4], p1 = b[5], p2 = b[6];
  uint16_t checksum = (uint16_t)((b[7] << 8) | b[8]);

  if (checksum != calc_checksum(ver, len, cmd, fb, p1, p2)) {
    // Likely just noise sliding through the window rather than a real
    // corrupted frame; silently ignore instead of reporting an error.
    return;
  }

  handle_command(chip, cmd, fb, p1, p2);
}

static void chip_uart_rx(void *user_data, uint8_t byte) {
  chip_state_t *chip = (chip_state_t *)user_data;
  memmove(chip->rx_buf, chip->rx_buf + 1, FRAME_LEN - 1);
  chip->rx_buf[FRAME_LEN - 1] = byte;

  if (chip->rx_buf[0] == START_BYTE && chip->rx_buf[FRAME_LEN - 1] == END_BYTE) {
    process_frame(chip);
  }
}

static void handle_trigger_release(chip_state_t *chip, bool is_io1, uint64_t press_ns) {
  if (press_ns == 0) return;
  uint64_t held_ns = get_sim_nanos() - press_ns;

  if (held_ns >= LONG_PRESS_NS) {
    if (is_io1) {
      if (chip->volume > 0) chip->volume--;
    } else {
      if (chip->volume < 30) chip->volume++;
    }
  } else {
    if (is_io1) {
      chip->current_track = (uint16_t)((chip->current_track + track_count(chip) - 1) % track_count(chip));
    } else {
      chip->current_track = (uint16_t)((chip->current_track + 1) % track_count(chip));
    }
    start_playing(chip);
  }
}

static void chip_io1_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (value == LOW) {
    chip->io1_press_ns = get_sim_nanos();
  } else {
    handle_trigger_release(chip, true, chip->io1_press_ns);
    chip->io1_press_ns = 0;
  }
}

static void chip_io2_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (value == LOW) {
    chip->io2_press_ns = get_sim_nanos();
  } else {
    handle_trigger_release(chip, false, chip->io2_press_ns);
    chip->io2_press_ns = 0;
  }
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));

  pin_t pin_rx = pin_init("RX", INPUT);
  pin_t pin_tx = pin_init("TX", OUTPUT);
  chip->pin_busy = pin_init("BUSY", OUTPUT_HIGH);
  chip->pin_io1 = pin_init("IO1", INPUT_PULLUP);
  chip->pin_io2 = pin_init("IO2", INPUT_PULLUP);

  chip->track_count_attr = attr_init("trackCount", 999);
  chip->track_duration_attr = attr_init("trackDurationMs", 3000);

  const uart_config_t uart_cfg = {
    .rx = pin_rx,
    .tx = pin_tx,
    .baud_rate = 9600,
    .rx_data = chip_uart_rx,
    .user_data = chip,
  };
  chip->uart = uart_init(&uart_cfg);

  const timer_config_t track_timer_cfg = {
    .callback = on_track_timer,
    .user_data = chip,
  };
  chip->track_timer = timer_init(&track_timer_cfg);

  const timer_config_t boot_timer_cfg = {
    .callback = on_boot_timer,
    .user_data = chip,
  };
  chip->boot_timer = timer_init(&boot_timer_cfg);

  const pin_watch_config_t io1_watch = {
    .edge = BOTH,
    .pin_change = chip_io1_change,
    .user_data = chip,
  };
  pin_watch(chip->pin_io1, &io1_watch);

  const pin_watch_config_t io2_watch = {
    .edge = BOTH,
    .pin_change = chip_io2_change,
    .user_data = chip,
  };
  pin_watch(chip->pin_io2, &io2_watch);

  reset_state(chip);
}
