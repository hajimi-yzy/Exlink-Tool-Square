#pragma once

#include <lvgl.h>

namespace exlink {

// Run in the LVGL/app task, including from the pointer read callback. The
// persistent act_obj is required here: lv_indev_get_obj_act() is cleared at
// the end of each previous input timer and can be NULL during read_cb.
inline void cancelLvglPointer(lv_indev_t *pointer) {
  if (!pointer || lv_indev_get_type(pointer) != LV_INDEV_TYPE_POINTER) return;
  lv_obj_t *target = pointer->proc.types.pointer.act_obj;
  if (target && lv_obj_is_valid(target))
    (void)lv_event_send(target, LV_EVENT_PRESS_LOST, pointer);
  // PRESS_LOST clears a widget's PRESSED state without RELEASED/CLICKED or
  // checkable-state changes. Its callback may delete target; do not reuse it.
  lv_indev_wait_release(pointer);
  lv_indev_reset(pointer, NULL);
}

} // namespace exlink
