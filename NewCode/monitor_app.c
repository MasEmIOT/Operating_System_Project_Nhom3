/**
 * @file monitor_app.c
 * @brief User Space Application
 * @details App này thực hiện:
 * 1. Chọn Driver (Thực tế hoặc Giả lập)
 * 2. Gọi System Call read() để lấy dữ liệu từ Kernel
 * 3. Hiển thị thông số và biểu đồ giả lập (OLED Simulation)
 * 4. Theo dõi log kernel (dmesg) thời gian thực
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>

#define DEVICE_NODE "/dev/mpu6050"

/* Cấu trúc dữ liệu khớp với Kernel Module */
struct mpu_data {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

int keep_running = 1;
int current_driver_mode = 0; // 0: None, 1: Kernel Driver, 2: Simulation

// Biến lưu trữ log kernel
char last_kernel_log[256] = "Waiting for kernel activity...";

/**
 * @brief Thread chuyên dụng để đọc log kernel
 * Giả lập việc 'tail -f' log kernel để hiển thị quá trình OS xử lý
 */
void *kernel_log_monitor(void *arg) {
    FILE *fp;
    char buffer[1024];
    
    // Xóa bộ đệm log cũ để đọc cái mới nhất
    system("sudo dmesg -c > /dev/null");

    while (keep_running) {
        // Đọc log kernel mới nhất liên quan đến MPU6050
        fp = popen("dmesg | grep MPU6050 | tail -n 1", "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp) != NULL) {
                // Cắt bỏ timestamp quá dài nếu cần
                strncpy(last_kernel_log, buffer, 255);
                // Xóa ký tự xuống dòng
                last_kernel_log[strcspn(last_kernel_log, "\n")] = 0;
            }
            pclose(fp);
        }
        usleep(500000); // 500ms update rate
    }
    return NULL;
}

/**
 * @brief Vẽ thanh biểu đồ đơn giản (ASCII)
 * Thay thế cho màn hình OLED trong môi trường terminal
 */
void draw_bar(const char* label, short value, int max_val) {
    int bar_len = 40;
    int pos = ((int)value + max_val) * bar_len / (2 * max_val);
    if (pos < 0) pos = 0;
    if (pos > bar_len) pos = bar_len;

    printf("%s: [", label);
    for (int i = 0; i < bar_len; i++) {
        if (i == bar_len/2) printf("|"); // Center line
        else if (i < pos) printf("#");
        else printf(" ");
    }
    printf("] (%d)\n", value);
}

void print_ui(struct mpu_data data) {
    // Xóa màn hình (dùng escape code)
    printf("\033[H\033[J");
    printf("\n[DEVICE STATUS]\n");
    if (current_driver_mode == 1) printf("Driver: Kernel Module (/dev/mpu6050)\n");
    else if (current_driver_mode == 2) printf("Driver: Simulation Mode\n");
    else printf("Driver: Disconnected\n");

    printf("\n[DATA VISUALIZATION (OLED SIMULATION)]\n");
    draw_bar("ACC X", data.accel_x, 17000);
    draw_bar("ACC Y", data.accel_y, 17000);
    draw_bar("ACC Z", data.accel_z, 17000);

    printf("\n[SYSTEM PROCESS VIEW (Kernel Activity)]\n");
    printf(">> Last Syscall Log: %s\n", last_kernel_log);
    printf("Ctrl+C to Exit.\n");
}

int main() {
    int fd = -1;
    struct mpu_data data;
    pthread_t log_thread;
    int choice;

    printf("Select Driver Mode:\n");
    printf("1. Use Real Kernel Driver (/dev/mpu6050)\n");
    printf("2. Use Simulation Driver (No Hardware Required)\n");
    printf("Choice: ");
    scanf("%d", &choice);

    if (choice == 1) {
        current_driver_mode = 1;
        printf("Opening /dev/mpu6050...\n");
        fd = open(DEVICE_NODE, O_RDWR);
        if (fd < 0) {
            perror("Failed to open device driver. Is the module loaded?");
            return -1;
        }
        // Khởi chạy thread giám sát log kernel
        pthread_create(&log_thread, NULL, kernel_log_monitor, NULL);
    } else {
        current_driver_mode = 2;
        printf("Starting Simulation Mode...\n");
        srand(time(NULL)); // Khởi tạo seed cho random
    }

    // Main Loop
    while (keep_running) {
        if (current_driver_mode == 1) {
            // GỌI SYSTEM CALL READ
            // App (User) -> Kernel (Driver) -> Hardware
            int ret = read(fd, &data, sizeof(data));
            if (ret < 0) {
                perror("Read failed");
                break;
            }
        } else {
            // Giả lập dữ liệu dao động
            data.accel_x = (rand() % 20000) - 10000;
            data.accel_y = (rand() % 20000) - 10000;
            data.accel_z = (rand() % 20000) - 10000;
            snprintf(last_kernel_log, 255, "Simulation: Generated random dataset %d", rand()%100);
        }

        print_ui(data);
        usleep(200000); // 200ms refresh rate
    }

    if (fd > 0) close(fd);
    keep_running = 0;
    if (current_driver_mode == 1) pthread_join(log_thread, NULL);

    return 0;
}
