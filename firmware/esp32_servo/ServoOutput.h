#pragma once
#include <driver/mcpwm_prelude.h>
#include "config.h"
class ServoOutput {
 public:
  void begin() {
    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_timer_config_t t = {};
    t.group_id = 0; t.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    t.resolution_hz = 1000000; t.period_ticks = 20000; t.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    ESP_ERROR_CHECK(mcpwm_new_timer(&t, &timer));
    mcpwm_oper_handle_t oper = nullptr;
    mcpwm_operator_config_t o = {}; o.group_id = 0;
    ESP_ERROR_CHECK(mcpwm_new_operator(&o, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));
    mcpwm_comparator_config_t c = {}; c.flags.update_cmp_on_tez = true;
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &c, &comparator_));
    write(CLOSED_ANGLE);
    mcpwm_gen_handle_t gen = nullptr;
    mcpwm_generator_config_t g = {}; g.gen_gpio_num = PIN_SERVO;
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &g, &gen));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(gen,
      MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(gen,
      MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator_, MCPWM_GEN_ACTION_LOW)));
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
  }
  void write(int angle) {
    const uint32_t pulse = SERVO_MIN_US + uint32_t(angle) * (SERVO_MAX_US - SERVO_MIN_US) / 180;
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator_, pulse));
  }
 private: mcpwm_cmpr_handle_t comparator_ = nullptr;
};
