/*
 * sysmond.c - System Monitoring Daemon for FRDM-IMX93
 *
 * Monitors CPU usage and system temperature, logs via syslog.
 * K2 button: LED1 color indicates CPU temperature (BLUE/WHITE/RED)
 * K3 button: LED1 color indicates CPU usage (GREEN/YELLOW/RED)
 *
 * LED1 RGB PWM mapping:
 *   Red   -> pwmchip4 pwm2  (GPIO_IO13 -> TPM4_CH2)
 *   Blue  -> pwmchip0 pwm2  (GPIO_IO12 -> TPM3_CH2)
 *   Green -> pwmchip0 pwm0  (GPIO_IO04 -> TPM3_CH0)
 *
 * Buttons via input event device:
 *   K2 -> /dev/input/event1  BTN_1 (code 257), value 0=pressed 1=released
 *   K3 -> /dev/input/event1  BTN_2 (code 258), value 0=pressed 1=released
 */

#include <stdio.h>  // fopen, fgets, fscanf, printf
#include <stdlib.h> // EXIT_SUCCESS, EXIT_FAILURE
#include <string.h> // strlen, strcmp
#include <unistd.h> // write, read, close, access, usleep
#include <signal.h> // signal(), SIGTERM, SIGINT
#include <syslog.h> // openlog(), syslog(), closelog()
#include <fcntl.h>  // open(), O_WRONLY, O_RDONLY, O_NONBLOCK
#include <errno.h>  // errno, strerror()
#include <time.h>   // time(), time_t
#include <linux/input.h> // input_event, EV_KEY, BTN_1, BTN_2
#include <sys/ioctl.h>   // ioctl() - included for completeness

#define DEFAULT_LOG_INTERVAL_SEC   5
#define CONFIG_FILE                "/etc/sysmond.conf"

#define PWM_RED_CHIP    "pwmchip4"
#define PWM_RED_CH      "pwm2"
#define PWM_BLUE_CHIP   "pwmchip0"
#define PWM_BLUE_CH     "pwm2"
#define PWM_GREEN_CHIP  "pwmchip0"
#define PWM_GREEN_CH    "pwm0"
#define PWM_BASE        "/sys/class/pwm"
#define PWM_PERIOD_NS   1000000
/* 1,000,000 nanoseconds = 1ms = 1kHz frequency.*/

#define BUTTON_DEV      "/dev/input/event1"
#define BTN_K2_CODE     257   /* BTN_1 */
#define BTN_K3_CODE     258   /* BTN_2 */
#define BTN_PRESSED     0
#define BTN_RELEASED    1

#define TEMP_BLUE_MAX   40
#define TEMP_WHITE_MAX  45

#define CPU_GREEN_MAX   60
#define CPU_YELLOW_MAX  80

static volatile int running = 1;
static int log_interval = DEFAULT_LOG_INTERVAL_SEC;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int pwm_write(const char *chip, const char *channel,
                     const char *attr, const char *value)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/%s/%s", PWM_BASE, chip, channel, attr);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t r = write(fd, value, strlen(value));
    close(fd);
    return (r < 0) ? -1 : 0;
}

static int pwm_export(const char *chip, int channel_num)
{
    // Before a PWM channel can be used, it must be exported — this tells the kernel to create the sysfs control files for that channel. 
    char path[256];
    char ch_path[256];
    char num[8];
    snprintf(ch_path, sizeof(ch_path), "%s/%s/pwm%d", PWM_BASE, chip, channel_num);
    if (access(ch_path, F_OK) == 0) return 0;
    snprintf(path, sizeof(path), "%s/%s/export", PWM_BASE, chip);
    snprintf(num, sizeof(num), "%d", channel_num);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t r = write(fd, num, strlen(num));
    close(fd);
    if (r < 0) return -1;
    usleep(50000); // waits 50ms for the kernel to create the files.
    return 0;
}

static void pwm_init_channel(const char *chip, const char *channel, int ch_num)
{
    char period[16];
    snprintf(period, sizeof(period), "%d", PWM_PERIOD_NS);
    pwm_export(chip, ch_num);
    pwm_write(chip, channel, "period", period);
    pwm_write(chip, channel, "duty_cycle", "0");
    pwm_write(chip, channel, "enable", "1");
}

static void pwm_set_duty(const char *chip, const char *channel, int percent)
{
    char duty[16];
    int val = (PWM_PERIOD_NS * percent) / 100;
    snprintf(duty, sizeof(duty), "%d", val);
    pwm_write(chip, channel, "duty_cycle", duty);
}

static void led_set_rgb(int r, int g, int b)
{
    pwm_set_duty(PWM_RED_CHIP,   PWM_RED_CH,   r);
    pwm_set_duty(PWM_GREEN_CHIP, PWM_GREEN_CH, g);
    pwm_set_duty(PWM_BLUE_CHIP,  PWM_BLUE_CH,  b);
}

static void led_off(void)    { led_set_rgb(0,   0,   0);   }
static void led_red(void)    { led_set_rgb(100, 0,   0);   }
static void led_green(void)  { led_set_rgb(0,   100, 0);   }
static void led_blue(void)   { led_set_rgb(0,   0,   100); }
static void led_white(void)  { led_set_rgb(100, 100, 100); }
static void led_yellow(void) { led_set_rgb(100, 100, 0);   }

typedef struct { long user, nice, sys, idle, iowait, irq, softirq; } CpuStat;
// The `/proc/stat` file exposes CPU time counters in **jiffies** (kernel timer ticks)

static int read_cpu_stat(CpuStat *s)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    int r = fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld",
                   &s->user, &s->nice, &s->sys, &s->idle,
                   &s->iowait, &s->irq, &s->softirq);
    fclose(f);
    return (r == 7) ? 0 : -1;
}

static float get_cpu_usage(void)
{
    /* CPU usage is a rate — you can't calculate it from a single snapshot. 
    You need to measure how many ticks were spent idle vs busy **over a time period**. 
    The formula is:     CPU% = 100 * (total_ticks - idle_ticks) / total_ticks 
    The 200ms gap gives a reasonable sample window without being too slow */
    
    CpuStat s1, s2;
    if (read_cpu_stat(&s1) < 0) return 0.0f;
    usleep(200000);
    if (read_cpu_stat(&s2) < 0) return 0.0f;
    long idle1  = s1.idle + s1.iowait;
    long idle2  = s2.idle + s2.iowait;
    long total1 = s1.user + s1.nice + s1.sys + idle1 + s1.irq + s1.softirq;
    long total2 = s2.user + s2.nice + s2.sys + idle2 + s2.irq + s2.softirq;
    long dtotal = total2 - total1;
    long didle  = idle2  - idle1;
    if (dtotal == 0) return 0.0f;
    return 100.0f * (float)(dtotal - didle) / (float)dtotal;
}

static float get_cpu_temp(void)
{
    /*
    The kernel thermal framework exposes temperatures in millidegrees Celsius. Reading 41800 from the file means 41.8°C. Dividing by 1000 converts to degrees.
    The function tries multiple thermal zones in order — different SoCs expose temperature on different zones. Returning -1.0f signals an error to the caller.
    */
    const char *paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        long raw = 0;
        int r = fscanf(f, "%ld", &raw);
        fclose(f);
        if (r == 1 && raw > 0)
            return (float)raw / 1000.0f;
    }
    return -1.0f;
}

static void load_config(void)
{
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        syslog(LOG_INFO, "Config file not found, using defaults");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int val;
        if (sscanf(line, "log_interval=%d", &val) == 1 && val > 0) {
            log_interval = val;
            syslog(LOG_INFO, "log_interval set to %d seconds", log_interval);
        }
    }
    fclose(f);
}

int main(void)
{
    // 1. Register signal handlers
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);

    // 2. Open syslog
    openlog("sysmond", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "sysmond starting up");

    // 3. Load config
    load_config();

    // 4. Initialize PWM channels and turn LED off
    pwm_init_channel(PWM_RED_CHIP,   PWM_RED_CH,   2);
    pwm_init_channel(PWM_BLUE_CHIP,  PWM_BLUE_CH,  2);
    pwm_init_channel(PWM_GREEN_CHIP, PWM_GREEN_CH, 0);
    led_off();

    // 5. Open button input device in NON-BLOCKING mode
    /*
    Without O_NONBLOCK, read() would block (pause the program) waiting for a button event. 
    With it, read() returns immediately with EAGAIN if no events are pending, allowing the loop to continue running.
    */
    int btn_fd = open(BUTTON_DEV, O_RDONLY | O_NONBLOCK);
    if (btn_fd < 0) {
        syslog(LOG_ERR, "Failed to open %s: %s", BUTTON_DEV, strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    syslog(LOG_INFO, "sysmond running (log_interval=%ds)", log_interval);

    time_t last_log = 0;
    int k2_pressed = 0;
    int k3_pressed = 0;
 
    // 6. Main loop
    while (running) {
        time_t now = time(NULL);

        /* Drain input events to update button state */
        struct input_event ev;
        while (read(btn_fd, &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_KEY) {
                if (ev.code == BTN_K2_CODE) k2_pressed = (ev.value == BTN_PRESSED);
                if (ev.code == BTN_K3_CODE) k3_pressed = (ev.value == BTN_PRESSED);
            }
        }
        /* Multiple events can queue up between loop iterations. 
        This inner loop reads all pending events before proceeding, ensuring no button press or release is missed.
        State tracking with k2_pressed/k3_pressed:
        Instead of reacting to events directly, the code tracks the current state of each button. 
        This means the LED logic only needs to check the current state, not worry about event ordering.
        */

        /* Periodic logging */
        if ((now - last_log) >= log_interval) {
            float temp  = get_cpu_temp();
            float usage = get_cpu_usage();
            if (temp >= 0)
                syslog(LOG_INFO, "CPU usage: %.1f%%, Temperature: %.1f degC",
                       usage, temp);
            else
                syslog(LOG_INFO, "CPU usage: %.1f%%, Temperature: unavailable",
                       usage);
            last_log = now;
        }

        /* LED control based on button state */
        if (k2_pressed) {
            float temp = get_cpu_temp();
            if (temp < 0)                   led_off();
            else if (temp < TEMP_BLUE_MAX)  led_blue();
            else if (temp < TEMP_WHITE_MAX) led_white();
            else                            led_red();
        } else if (k3_pressed) {
            float usage = get_cpu_usage();
            if (usage < CPU_GREEN_MAX)       led_green();
            else if (usage < CPU_YELLOW_MAX) led_yellow();
            else                             led_red();
        } else {
            led_off();
        }

        /* 100ms sleep */
        usleep(100000); 
        // Sleeps 100 milliseconds between iterations. This is responsive enough for human button presses while using almost no CPU — the daemon consumes ~0% CPU at idle.

    }

    led_off();
    close(btn_fd);
    syslog(LOG_INFO, "sysmond stopped");
    closelog();
    return EXIT_SUCCESS;
}
