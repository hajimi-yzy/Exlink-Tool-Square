/* Run the real driver function with deterministic RTOS/HAL fault injection.
 * These tests validate task-side control flow, not peripheral/ISR concurrency.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t TickType_t;
typedef TickType_t portTickType;
typedef int portBASE_TYPE;
typedef int esp_err_t;
typedef int i2c_port_t;
typedef void *i2c_cmd_handle_t;
enum { pdFALSE, pdTRUE };
enum { ESP_OK, ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE };
enum { I2C_MODE_MASTER, I2C_STATUS_IDLE, I2C_STATUS_READ, I2C_STATUS_TIMEOUT,
       I2C_STATUS_ACK_ERROR, I2C_STATUS_DONE };
enum { I2C_CMD_EVT_DONE, I2C_CMD_EVT_ALIVE };
#define I2C_NUM_MAX 2
#define I2C_CMD_ALIVE_INTERVAL_TICK 1000U
#define I2C_ACKERR_CNT_MAX 10
#define I2C_LL_INTR_MASK UINT32_MAX
#define CONFIG_SPIRAM_USE_MALLOC 0
/* Exercise the optional PM cleanup path too. */
#define CONFIG_PM_ENABLE 1
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ESP_RETURN_ON_FALSE(condition, error, ...) \
    do { if (!(condition)) return (error); } while (0)

typedef struct command_node { struct { unsigned bytes_used; } cmd; } command_node;
typedef struct { command_node *free, *cur, *head; } i2c_cmd_desc_t;
typedef struct {
    int mode, status, cmd_mux, pm_lock, cmd_evt_queue, cmd_idx, rx_cnt;
    i2c_cmd_desc_t cmd_link;
} i2c_obj_t;
typedef struct { int type; } i2c_cmd_evt_t;
static i2c_obj_t object;
static i2c_obj_t *p_i2c_obj[I2C_NUM_MAX];
static struct { int hal; } i2c_context[I2C_NUM_MAX];
static uint8_t clear_bus_cnt[I2C_NUM_MAX];

static struct {
    uint64_t now;
    TickType_t mutex_delay, start_delay;
    bool mutex_fails, locked, bus_busy;
    unsigned takes, gives, pm_acquires, pm_releases, resets, starts, queue_resets;
    unsigned event_count, next_event, wait_count;
    struct { uint64_t at; int type, status; } events[8];
    TickType_t waits[16];
} fake;
static command_node node;
static i2c_cmd_desc_t command;

static TickType_t xTaskGetTickCount(void) { return (TickType_t)fake.now; }
static int xSemaphoreTake(int mutex, TickType_t wait)
{
    (void)mutex;
    assert(!fake.locked);
    assert(fake.mutex_delay <= wait);
    fake.takes++;
    fake.now += fake.mutex_delay;
    if (fake.mutex_fails) return pdFALSE;
    fake.locked = true;
    return pdTRUE;
}
static void xSemaphoreGive(int mutex)
{
    (void)mutex;
    assert(fake.locked);
    fake.locked = false;
    fake.gives++;
}
static void esp_pm_lock_acquire(int lock) { (void)lock; fake.pm_acquires++; }
static void esp_pm_lock_release(int lock) { (void)lock; fake.pm_releases++; }
static void xQueueReset(int queue)
{
    (void)queue;
    fake.queue_resets++;
    /* events describe future ISR deliveries, injected after transfer start. */
}
static int xQueueReceive(int queue, i2c_cmd_evt_t *event, TickType_t wait)
{
    (void)queue;
    assert(fake.locked && fake.starts > 0);
    assert(fake.wait_count < sizeof(fake.waits) / sizeof(fake.waits[0]));
    fake.waits[fake.wait_count++] = wait;
    if (fake.next_event < fake.event_count &&
        fake.events[fake.next_event].at <= fake.now + wait) {
        unsigned index = fake.next_event++;
        if (fake.now < fake.events[index].at) fake.now = fake.events[index].at;
        event->type = fake.events[index].type;
        object.status = fake.events[index].status;
        return pdTRUE;
    }
    fake.now += wait;
    return pdFALSE;
}
static bool i2c_hal_is_bus_busy(int *hal) { (void)hal; return fake.bus_busy; }
static void i2c_hw_fsm_reset(i2c_port_t port)
{
    assert(port == 0 && fake.locked);
    fake.resets++;
    fake.bus_busy = false;
}
static void i2c_reset_tx_fifo(i2c_port_t port) { assert(port == 0); }
static void i2c_reset_rx_fifo(i2c_port_t port) { assert(port == 0); }
static void i2c_hal_disable_intr_mask(int *hal, uint32_t mask) { (void)hal; (void)mask; }
static void i2c_hal_clr_intsts_mask(int *hal, uint32_t mask) { (void)hal; (void)mask; }
static void i2c_master_cmd_begin_static(i2c_port_t port, void *woken)
{
    assert(port == 0 && woken == NULL && fake.locked);
    fake.starts++;
    fake.now += fake.start_delay;
    object.status = I2C_STATUS_READ;
}

#include "driver_function.inc"

static void reset_fixture(uint64_t now)
{
    memset(&fake, 0, sizeof(fake));
    memset(&object, 0, sizeof(object));
    memset(&node, 0, sizeof(node));
    memset(clear_bus_cnt, 0, sizeof(clear_bus_cnt));
    fake.now = now;
    object.mode = I2C_MODE_MASTER;
    object.status = I2C_STATUS_DONE;
    p_i2c_obj[0] = &object;
    command = (i2c_cmd_desc_t){ .head = &node };
}
static void add_event(uint64_t at, int type, int status)
{
    unsigned index = fake.event_count++;
    assert(index < sizeof(fake.events) / sizeof(fake.events[0]));
    fake.events[index].at = at;
    fake.events[index].type = type;
    fake.events[index].status = status;
}
static void assert_released(unsigned completed)
{
    assert(!fake.locked && fake.gives == completed);
    assert(fake.pm_acquires == completed && fake.pm_releases == completed);
    assert(object.status == I2C_STATUS_DONE);
}

static void test_missing_completion_and_next_transaction(void)
{
    reset_fixture(0);
    clear_bus_cnt[0] = 7;
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_ERR_TIMEOUT);
    assert(fake.now == 20 && fake.wait_count == 1 && fake.waits[0] == 20);
    assert(fake.resets == 1 && clear_bus_cnt[0] == 0);
    assert_released(1);
    /* Keep the same driver object and locks: recovery must permit a new call. */
    add_event(23, I2C_CMD_EVT_DONE, I2C_STATUS_IDLE);
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_OK);
    assert(fake.now == 23 && fake.resets == 1 && fake.starts == 2);
    assert(fake.queue_resets == 2);
    assert_released(2);
}
static void test_done_already_queued_at_deadline(void)
{
    reset_fixture(0);
    fake.start_delay = 20;
    add_event(20, I2C_CMD_EVT_DONE, I2C_STATUS_IDLE);
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_OK);
    assert(fake.now == 20 && fake.wait_count == 1 && fake.waits[0] == 0);
    assert(fake.resets == 0);
    assert_released(1);
}
static void test_alive_does_not_extend_budget(void)
{
    reset_fixture(0);
    add_event(5, I2C_CMD_EVT_ALIVE, I2C_STATUS_READ);
    add_event(15, I2C_CMD_EVT_ALIVE, I2C_STATUS_READ);
    add_event(20, I2C_CMD_EVT_ALIVE, I2C_STATUS_READ);
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_ERR_TIMEOUT);
    assert(fake.now == 20 && fake.wait_count == 4);
    assert(fake.waits[0] == 20 && fake.waits[1] == 15 &&
           fake.waits[2] == 5 && fake.waits[3] == 0);
    assert(fake.resets == 1);
    assert_released(1);
}
static void test_mutex_consumes_budget(void)
{
    reset_fixture(0);
    fake.mutex_delay = 20;
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_ERR_TIMEOUT);
    assert(fake.now == 20 && fake.wait_count == 1 && fake.waits[0] == 0);
    assert(fake.resets == 1);
    assert_released(1);
    reset_fixture(0);
    fake.mutex_delay = 20;
    fake.mutex_fails = true;
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_ERR_TIMEOUT);
    assert(fake.now == 20 && fake.starts == 0 && fake.wait_count == 0);
    assert(fake.gives == 0 && fake.pm_acquires == 0 && fake.pm_releases == 0);
    assert(!fake.locked && fake.resets == 0);
}
static void test_tick_wrap(void)
{
    const uint64_t start = UINT32_MAX - 9ULL;
    reset_fixture(start);
    add_event(start + 12, I2C_CMD_EVT_ALIVE, I2C_STATUS_READ);
    assert(i2c_master_cmd_begin(0, &command, 20) == ESP_ERR_TIMEOUT);
    assert(fake.now == start + 20 && xTaskGetTickCount() == 10);
    assert(fake.wait_count == 2 && fake.waits[0] == 20 && fake.waits[1] == 8);
    assert(fake.resets == 1);
    assert_released(1);
}

int main(void)
{
    test_missing_completion_and_next_transaction();
    test_done_already_queued_at_deadline();
    test_alive_does_not_extend_budget();
    test_mutex_consumes_budget();
    test_tick_wrap();
    puts("PASS: real vendored i2c_master_cmd_begin: timeout/recovery, deadline DONE, ALIVE budget, mutex budget/failure, tick wrap");
    return 0;
}
