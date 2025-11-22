// mpu_monitor.c
// Build: gcc -O2 -o mpu_monitor mpu_monitor.c -lncurses
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <ncurses.h>
#include <time.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static long get_total_jiffies(void) {
    FILE *f = fopen("/proc/stat","r");
    if(!f) return 0;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    fclose(f);
    char cpu[8];
    long user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0;
    int n = sscanf(line, "%s %ld %ld %ld %ld %ld %ld %ld %ld", cpu, &user,&nice,&system,&idle,&iowait,&irq,&softirq,&steal);
    long total = 0;
    if (n >= 5) total = user + nice + system + idle + (n>=6?iowait:0) + (n>=7?irq:0) + (n>=8?softirq:0) + (n>=9?steal:0);
    return total;
}

static long get_proc_jiffies(pid_t pid) {
    char path[64], buf[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if(!f) return 0;
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0; }
    fclose(f);
    unsigned long utime=0, stime=0;
    // utime is field 14, stime 15
    // sscanf with many skips
    sscanf(buf,
           "%*d %*s %*c %*d %*d %*d %*d %*d "
           "%*u %*u %*u %*u %*u %lu %lu",
           &utime, &stime);
    return (long)(utime + stime);
}

static double timespec_to_double(const struct timespec *t) {
    return t->tv_sec + t->tv_nsec/1e9;
}

int main(int argc, char **argv) {
    const char *dev = "/dev/mpu6050";
    if (argc > 1) dev = argv[1];

    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        perror("open /dev/mpu6050");
        return 1;
    }

    initscr();
    noecho();
    curs_set(FALSE);
    nodelay(stdscr, TRUE);

    pid_t pid = getpid();
    long prev_total = get_total_jiffies();
    long prev_proc = get_proc_jiffies(pid);
    struct timespec prev_ts;
    clock_gettime(CLOCK_MONOTONIC, &prev_ts);

    while (1) {
        uint8_t buf[6];
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        ssize_t r = read(fd, buf, 6);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (r != 6) {
            mvprintw(0,0,"read returned %ld", r);
            refresh();
            usleep(100000);
            continue;
        }

        int16_t ax = (int16_t)(buf[0] | (buf[1]<<8));
        int16_t ay = (int16_t)(buf[2] | (buf[3]<<8));
        int16_t az = (int16_t)(buf[4] | (buf[5]<<8));

        double t_read = timespec_to_double(&t1) - timespec_to_double(&t0);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double dt = timespec_to_double(&now) - timespec_to_double(&prev_ts);
        prev_ts = now;
        if (dt <= 0) dt = 1e-6;

        long total = get_total_jiffies();
        long proc = get_proc_jiffies(pid);
        long dtotal = total - prev_total;
        long dproc = proc - prev_proc;
        prev_total = total; prev_proc = proc;
        double cpu_usage = 0.0;
        if (dtotal > 0) cpu_usage = 100.0 * (double)dproc / (double)dtotal;

        mvprintw(0,0,"MPU6050 Realtime Monitor (device: %s)", dev);
        mvprintw(2,0,"Raw accel (LSB): Ax: %6d   Ay: %6d   Az: %6d", ax, ay, az);
        // convert to m/s^2 using scale 16384 LSB/g and g=9.80665
        double ax_ms2 = (double)ax * 9.80665 / 16384.0;
        double ay_ms2 = (double)ay * 9.80665 / 16384.0;
        double az_ms2 = (double)az * 9.80665 / 16384.0;
        mvprintw(3,0,"Accel (m/s^2):  Ax: %7.3f   Ay: %7.3f   Az: %7.3f", ax_ms2, ay_ms2, az_ms2);

        mvprintw(5,0,"Sample period dt: %.6f s   (%.2f Hz)", dt, 1.0/dt);
        mvprintw(6,0,"Read latency (read syscall): %.3f ms", t_read*1e3);
        mvprintw(7,0,"Process CPU usage (est): %.2f %%", cpu_usage);
        mvprintw(9,0,"Press q to quit.");

        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        /* No sleep: read as fast as device/driver allows.
           If you want to throttle, uncomment below */
        // usleep(5000);
        usleep(500000);
    }

    endwin();
    close(fd);
    return 0;
}
