#ifndef UPDATER_APP_H_
#define UPDATER_APP_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  UPDATER_APP_ACTION_NONE = 0,
  UPDATER_APP_ACTION_REBOOT,
  UPDATER_APP_ACTION_ENTER_UPDATER,
} updater_app_action_t;

bool updater_app_schedule_action(updater_app_action_t action);
void updater_app_notify_response_sent(void);
void updater_app_task(void);

#ifdef __cplusplus
}
#endif

#endif /* UPDATER_APP_H_ */
