// Real capture worker in a C++ thread. Every peripheral API below is a mock;
// no part of this program claims to exercise an ESP32 DMA engine.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "scope_capture.h"
#include "scope_math.h"
#include "app_log.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/task.h"
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
struct WorkerShutdown {};
struct MockTask {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned notifications = 0;
    bool shutdown = false;
    std::thread thread;
} task;
static bool fail_create;

int xTaskCreatePinnedToCore(void (*entry)(void *), const char *, unsigned stack,
                          void *argument, unsigned, TaskHandle_t *handle, unsigned)
{
    assert(stack >= 8192);
    if (fail_create) { fail_create = false; return 0; }
    assert(!task.thread.joinable());
    *handle = &task;
    task.thread = std::thread([=] { try { entry(argument); } catch (WorkerShutdown &) {} });
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t handle)
{
    assert(handle == &task);
    std::lock_guard<std::mutex> guard(task.mutex);
    ++task.notifications;
    task.changed.notify_all();
}
uint32_t ulTaskNotifyTake(int clear, TickType_t wait)
{
    assert(clear == pdTRUE && wait == portMAX_DELAY);
    std::unique_lock<std::mutex> guard(task.mutex);
    task.changed.wait(guard, [] { return task.notifications || task.shutdown; });
    if (task.shutdown) throw WorkerShutdown{};
    const unsigned result = task.notifications;
    task.notifications = 0;
    return result;
}
void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 2048; }
extern "C" int app_log_log(app_log_level_t, const char *, const char *, ...) { return 0; }

struct MockAdc {
    std::mutex mutex;
    std::condition_variable changed;
    bool initialized = false, started = false;
    bool hold_read = false, in_read = false, hold_teardown = false, in_teardown = false;
    bool hold_configure = false, in_configure = false;
    int fail_init = 0, fail_configure = 0, fail_start = 0, read_errors = 0, timeouts = 0;
    int fail_stop = 0, fail_deinitialize = 0;
    bool wrong_channel_once = false;
    unsigned initialize_calls = 0, stop_calls = 0, deinitialize_calls = 0;
    unsigned stop_attempts = 0, deinitialize_attempts = 0;
    unsigned allocated = 0, freed = 0, starts = 0, reads = 0;
    uint32_t rate = 0, phase = 0;
} adc;

esp_err_t adc_digi_initialize(const adc_digi_init_config_t *config)
{
    std::lock_guard<std::mutex> guard(adc.mutex);
    ++adc.initialize_calls;
    assert(!adc.initialized && !adc.started); // A second controller must never overlap.
    assert(config->max_store_buf_size >= 512 && config->conv_num_each_intr == 512);
    assert(config->adc1_chan_mask == (1U << ADC_CHANNEL_3));
    if (adc.fail_init) { --adc.fail_init; return ESP_FAIL; }
    adc.initialized = true;
    ++adc.allocated;
    return ESP_OK;
}
esp_err_t adc_digi_controller_configure(const adc_digi_configuration_t *config)
{
    std::unique_lock<std::mutex> guard(adc.mutex);
    assert(adc.initialized && !adc.started);
    assert(config->pattern_num == 1 && config->adc_pattern->channel == ADC_CHANNEL_3);
    assert(config->adc_pattern->unit == 0 && config->adc_pattern->bit_width == 12);
    assert(config->conv_mode == ADC_CONV_SINGLE_UNIT_1 && config->format == ADC_DIGI_OUTPUT_FORMAT_TYPE2);
    adc.rate = config->sample_freq_hz;
    adc.in_configure = true;
    adc.changed.notify_all();
    adc.changed.wait(guard, [] { return !adc.hold_configure; });
    adc.in_configure = false;
    if (adc.fail_configure) { --adc.fail_configure; return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t adc_digi_start()
{
    std::lock_guard<std::mutex> guard(adc.mutex);
    assert(adc.initialized && !adc.started);
    if (adc.fail_start) { --adc.fail_start; return ESP_FAIL; }
    adc.started = true;
    adc.phase = 0;
    ++adc.starts;
    return ESP_OK;
}
esp_err_t adc_digi_read_bytes(uint8_t *output, uint32_t capacity, uint32_t *length, uint32_t timeout)
{
    assert(timeout == 10 && capacity == 512);
    std::this_thread::sleep_for(1ms); // Bound frame throughput; avoid a spinning mock.
    std::unique_lock<std::mutex> guard(adc.mutex);
    assert(adc.initialized && adc.started);
    ++adc.reads;
    adc.in_read = true;
    adc.changed.notify_all();
    adc.changed.wait(guard, [] { return !adc.hold_read; });
    adc.in_read = false;
    *length = 0;
    if (adc.timeouts) { --adc.timeouts; return ESP_ERR_TIMEOUT; }
    if (adc.read_errors) { --adc.read_errors; return ESP_FAIL; }
    const bool wrong = adc.wrong_channel_once;
    adc.wrong_channel_once = false;
    for (unsigned offset = 0; offset < capacity; offset += sizeof(adc_digi_output_data_t)) {
        adc_digi_output_data_t sample = {};
        sample.type2.channel = wrong ? 2 : ADC_CHANNEL_3;
        // A deterministic 40-sample square wave, expressed as fake calibrated mV.
        sample.type2.data = (adc.phase++ % 40 < 20) ? 1000 : 2000;
        std::memcpy(output + offset, &sample, sizeof(sample));
    }
    *length = capacity;
    return ESP_OK;
}
esp_err_t adc_digi_stop()
{
    std::lock_guard<std::mutex> guard(adc.mutex);
    assert(adc.initialized && adc.started);
    ++adc.stop_attempts;
    if (adc.fail_stop) { --adc.fail_stop; return ESP_FAIL; }
    adc.started = false;
    ++adc.stop_calls;
    return ESP_OK;
}
esp_err_t adc_digi_deinitialize()
{
    std::unique_lock<std::mutex> guard(adc.mutex);
    assert(adc.initialized);
    ++adc.deinitialize_attempts;
    if (adc.started) return ESP_FAIL;
    if (adc.fail_deinitialize) { --adc.fail_deinitialize; return ESP_FAIL; }
    adc.in_teardown = true;
    adc.changed.notify_all();
    adc.changed.wait(guard, [] { return !adc.hold_teardown; });
    adc.in_teardown = false;
    adc.initialized = false;
    ++adc.deinitialize_calls;
    ++adc.freed;
    return ESP_OK;
}
const char *esp_err_to_name(esp_err_t) { return "MOCK_ADC_ERROR"; }
int esp_adc_cal_characterize(unsigned, unsigned, unsigned, unsigned,
                             esp_adc_cal_characteristics_t *) { return ESP_ADC_CAL_VAL_EFUSE_TP_FIT; }
uint32_t esp_adc_cal_raw_to_voltage(uint32_t raw, const esp_adc_cal_characteristics_t *) { return raw; }

template<class Predicate> static void wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}
template<class Operation> static void adc_change(Operation operation)
{
    std::lock_guard<std::mutex> guard(adc.mutex);
    operation();
    adc.changed.notify_all();
}
template<class Predicate> static void wait_adc(Predicate predicate)
{
    std::unique_lock<std::mutex> guard(adc.mutex);
    assert(adc.changed.wait_for(guard, 2s, predicate));
}
static void expect_released(scope_state_t expected)
{
    wait_until([] { return !scope_adc_owned(); });
    assert(scope_acq_state() == expected);
    std::lock_guard<std::mutex> guard(adc.mutex);
    assert(!adc.initialized && !adc.started && adc.allocated == adc.freed);
}
static scope_frame_t next_frame(uint32_t after, const scope_config_t &config)
{
    scope_frame_t frame = {};
    wait_until([&] { return scope_acq_read(&frame, after); });
    assert(frame.sequence > after && frame.count == scope_point_count(config.time_div_ms));
    assert(frame.sample_hz == scope_sample_hz(config.time_div_ms) && frame.calibrated);
    assert(frame.min_mv == 1000 && frame.max_mv == 2000);
    assert(frame.mean_mv >= 1000 && frame.mean_mv <= 2000 && !frame.clipped);
    for (unsigned i = 0; i < frame.count; ++i) assert(frame.mv[i] == 1000 || frame.mv[i] == 2000);
    return frame;
}
static void stop_without_waiting_for_adc()
{
    auto stopped = std::async(std::launch::async, [] { scope_acq_stop(); });
    // read_bytes remains held by the test. STOP must finish independently.
    assert(stopped.wait_for(100ms) == std::future_status::ready);
    stopped.get();
}

static void shutdown_worker()
{
    {
        std::lock_guard<std::mutex> guard(task.mutex);
        task.shutdown = true;
        task.changed.notify_all();
    }
    task.thread.join();
}

// Each fatal teardown case runs in its own process: production deliberately
// requires a reboot after uncertain release; there is no test-only reset hook.
static void teardown_failure(const char *mode)
{
    const bool stop_failure = std::strcmp(mode, "stop") == 0;
    const bool configure_failure = std::strcmp(mode, "configure-deinit") == 0;
    const bool start_failure = std::strcmp(mode, "start-deinit") == 0;
    assert(stop_failure || configure_failure || start_failure || std::strcmp(mode, "deinit") == 0);
    scope_config_t config = {2, 1500, SCOPE_RISING};
    if (configure_failure || start_failure) {
        adc.fail_configure = configure_failure ? 1 : 0;
        adc.fail_start = start_failure ? 1 : 0;
        adc.fail_deinitialize = 1;
        assert(scope_acq_start(&config));
    } else {
        adc.hold_read = true;
        assert(scope_acq_start(&config));
        wait_adc([] { return adc.in_read; });
        adc_change([=] {
            adc.fail_stop = stop_failure ? 1 : 0;
            adc.fail_deinitialize = stop_failure ? 0 : 1;
        });
        stop_without_waiting_for_adc();
        adc_change([] { adc.hold_read = false; });
    }
    wait_until([] { return scope_acq_state() == SCOPE_ERROR; });
    assert(scope_adc_owned());
    assert(!scope_acq_quiesce());
    assert(scope_acq_state() == SCOPE_ERROR && scope_adc_owned());
    assert(!scope_acq_start(&config) && !scope_acq_resume());
    // A further STOP notification must not clear the fatal reservation or retry
    // driver teardown. Quiesce gives the worker ample time to consume it.
    assert(!scope_acq_quiesce());
    shutdown_worker();
    assert(scope_acq_state() == SCOPE_ERROR && scope_adc_owned());
    assert(adc.initialize_calls == 1 && adc.allocated == 1 && adc.freed == 0);
    assert(adc.initialized && adc.started == stop_failure);
    assert(adc.deinitialize_attempts == 1 && adc.deinitialize_calls == 0);
    assert(adc.stop_attempts == (configure_failure || start_failure ? 0U : 1U));
    printf("PASS: %s failure retains ADC reservation, ERROR, failed quiesce and blocked START/resume; teardown is not retried.\n", mode);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2) { teardown_failure(argv[1]); return 0; }
    assert(argc == 1);
    scope_config_t fast = {2, 1500, SCOPE_RISING};
    scope_config_t middle = {10, 1500, SCOPE_FREE};
    scope_config_t slow = {20, 1500, SCOPE_FALLING};
    scope_config_t invalid = {3, 1500, SCOPE_FREE};
    assert(!scope_acq_start(nullptr) && !scope_acq_start(&invalid));
    invalid = fast; invalid.trigger = static_cast<scope_trigger_t>(9);
    assert(!scope_acq_start(&invalid));
    fail_create = true;
    assert(!scope_acq_start(&fast) && !scope_adc_owned());

    adc_change([] { adc.hold_read = true; });
    assert(scope_acq_start(&fast));
    assert(scope_adc_owned());
    wait_adc([] { return adc.in_read; });
    stop_without_waiting_for_adc();
    assert(scope_adc_owned()); // Still reserved until real worker teardown.
    adc_change([] { adc.hold_teardown = true; adc.hold_read = false; });
    wait_adc([] { return adc.in_teardown; });
    assert(scope_adc_owned());
    assert(scope_acq_start(&middle) && scope_acq_start(&slow));
    assert(scope_acq_state() == SCOPE_STARTING && scope_adc_owned());
    adc_change([] { adc.hold_teardown = false; });
    scope_frame_t frame = next_frame(0, slow);
    assert(scope_acq_state() == SCOPE_RUNNING);
    puts("PASS: STOP returns while ADC read is held; reservation lasts through teardown; newest rapid START/reconfigure wins.");

    for (unsigned i = 0; i < 30; ++i) {
        const scope_frame_t snapshot = frame;
        frame = next_frame(frame.sequence, slow);
        assert(snapshot.count == 501 && snapshot.sample_hz == 2500);
        assert(snapshot.min_mv == 1000 && snapshot.max_mv == 2000);
    }
    adc_change([] { adc.hold_read = true; });
    wait_adc([] { return adc.in_read; });
    scope_frame_t held = {};
    assert(scope_acq_read(&held, 0));
    assert(!scope_acq_read(&held, held.sequence));
    auto quiet = std::async(std::launch::async, [] { return scope_acq_quiesce(); });
    assert(quiet.wait_for(20ms) == std::future_status::timeout);
    // In particular, an older generation must not clear the running owner's
    // reservation and let quiesce succeed while read_bytes is still held.
    adc_change([] { adc.hold_read = false; });
    assert(quiet.get());
    expect_released(SCOPE_STOPPED);
    const uint32_t saved_sequence = frame.sequence;
    assert(scope_acq_resume());
    frame = next_frame(saved_sequence, slow);
    puts("PASS: copied frames remain consistent; sequence increases; quiesce releases ADC and resume restores configuration.");

    adc_change([] { adc.hold_read = true; });
    wait_adc([] { return adc.in_read; });
    assert(!scope_acq_quiesce()); // Explicit bounded failure with an unresponsive ADC mock.
    assert(scope_adc_owned());
    adc_change([] { adc.hold_read = false; });
    expect_released(SCOPE_STOPPED);
    assert(scope_acq_resume());
    frame = next_frame(frame.sequence, slow);

    adc_change([] { adc.timeouts = 3; adc.read_errors = 1; adc.wrong_channel_once = true; });
    const uint32_t before_drop = frame.dropped;
    wait_until([&] {
        scope_frame_t candidate;
        if (!scope_acq_read(&candidate, frame.sequence)) return false;
        frame = candidate;
        return frame.dropped > before_drop;
    });
    assert(scope_acq_state() == SCOPE_RUNNING);
    frame = next_frame(frame.sequence, slow);
    puts("PASS: bounded quiesce timeout retains ownership; transient timeout/overflow/foreign channel recover to intact frames.");

    adc_change([] { adc.read_errors = 10; });
    wait_until([] { return scope_acq_state() == SCOPE_ERROR; });
    expect_released(SCOPE_ERROR);
    for (int stage = 0; stage < 3; ++stage) {
        adc_change([=] {
            if (stage == 0) adc.fail_init = 1;
            if (stage == 1) adc.fail_configure = 1;
            if (stage == 2) adc.fail_start = 1;
        });
        assert(scope_acq_start(&fast));
        wait_until([] { return scope_acq_state() == SCOPE_ERROR; });
        expect_released(SCOPE_ERROR);
    }
    assert(scope_acq_start(&fast));
    frame = next_frame(frame.sequence, fast);
    assert(scope_acq_quiesce());
    expect_released(SCOPE_STOPPED);
    puts("PASS: 10 consecutive read errors and init/configure/start failures release resources; a later START recovers.");

    // The failure callback of an older generation must preserve a newer START,
    // just as the normal teardown callback does.
    adc_change([] { adc.hold_configure = true; adc.fail_configure = 1; });
    assert(scope_acq_start(&fast));
    wait_adc([] { return adc.in_configure; });
    assert(scope_acq_start(&slow));
    adc_change([] { adc.hold_configure = false; });
    frame = next_frame(frame.sequence, slow);
    adc_change([] { adc.hold_read = true; });
    wait_adc([] { return adc.in_read; });
    quiet = std::async(std::launch::async, [] { return scope_acq_quiesce(); });
    assert(quiet.wait_for(20ms) == std::future_status::timeout);
    adc_change([] { adc.hold_read = false; });
    assert(quiet.get());
    expect_released(SCOPE_STOPPED);
    puts("PASS: an old configuration failure cannot clear the reservation of a newer START.");

    for (unsigned i = 0; i < 25; ++i) {
        assert(scope_acq_start(i % 2 ? &fast : &middle));
        frame = next_frame(frame.sequence, i % 2 ? fast : middle);
        assert(scope_acq_quiesce());
        expect_released(SCOPE_STOPPED);
    }
    shutdown_worker();
    assert(adc.allocated == adc.freed && adc.starts == adc.stop_calls);
    printf("PASS: 25 threaded restart cycles; ADC allocations/releases %u/%u; starts/stops %u/%u; stable sequence %lu.\n",
           adc.allocated, adc.freed, adc.starts, adc.stop_calls, static_cast<unsigned long>(frame.sequence));
    puts("MOCK HARDWARE ONLY: no real ADC/DMA timing, calibration, electrical accuracy, or FreeRTOS scheduling validated.");
}
