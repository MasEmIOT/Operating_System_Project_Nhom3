#include <gtk/gtk.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define DEVICE_NODE "/dev/mpu6050"
#define KERNEL_DIR ".."

struct mpu_data {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

typedef struct {
    GtkWidget *window;
    GtkWidget *label_status;
    GtkWidget *label_log;
    GtkWidget *prog_x, *prog_y, *prog_z;
    GtkWidget *btn_start, *btn_stop;

    // Nút quản lý module
    GtkWidget *btn_build;
    GtkWidget *btn_clean;        // <--- NÚT MỚI
    GtkWidget *btn_load_soft_i2c;
    GtkWidget *btn_load_mpu6050;
    GtkWidget *btn_load_ssd1306;
    GtkWidget *btn_unload_soft_i2c;
    GtkWidget *btn_unload_mpu6050;
    GtkWidget *btn_unload_ssd1306;
    GtkWidget *btn_clear_log;

    GtkWidget *log_scrolled_window;
    GtkWidget *log_text_view;
    GtkTextBuffer *log_buffer;

    int fd;
    int running;
    int simulation_mode;
    struct mpu_data data;
    char kernel_log[1024];

    GtkWidget *drawing_area;
    double history_x[100];
    double history_y[100];
    double history_z[100];
    int history_index;
    int history_count;
} App;

static App app = {0};
static void on_clear_log_clicked(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(app.log_buffer, "=== LOG CLEARED ===\nSẵn sàng cho hành động mới!\n\n", -1);
}
// Hàm run_command mới - cải thiện thu thập output đầy đủ từ make
// Hàm run_command mới - tương thích GTK 4, hiển thị log realtime đầy đủ
static void run_command(const char *cmd, const char *success_msg) {
    char full_cmd[2048];
    snprintf(full_cmd, sizeof(full_cmd), "/bin/bash -c \"%s 2>&1\"", cmd);

    // Thông báo đang chạy
    gtk_text_buffer_set_text(app.log_buffer, "Đang thực thi lệnh...\nVui lòng chờ (có thể mất vài giây)...\n\n", -1);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        gtk_text_buffer_set_text(app.log_buffer, "Lỗi: Không thể thực thi lệnh!\n", -1);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Xóa ký tự xuống dòng
        line[strcspn(line, "\r\n")] = '\0';

        // Thêm dòng vào buffer
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(app.log_buffer, &end);
        gtk_text_buffer_insert(app.log_buffer, &end, line, -1);
        gtk_text_buffer_insert(app.log_buffer, &end, "\n", -1);

        // TỰ ĐỘNG CUỘN XUỐNG CUỐI
        GtkTextMark *mark = gtk_text_buffer_get_insert(app.log_buffer);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app.log_text_view), mark, 0.0, FALSE, 0.0, 0.0);

        // Cập nhật giao diện mà không dùng hàm deprecated
        // Cách đúng trong GTK4: dùng GLib main loop iteration
        GMainContext *context = g_main_context_default();
        while (g_main_context_iteration(context, FALSE));
    }

    int status = pclose(fp);

    // Thêm thông báo kết quả
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app.log_buffer, &end);

    if (status == 0 && success_msg) {
        char msg[512];
        snprintf(msg, sizeof(msg), "\n%s\n", success_msg);
        gtk_text_buffer_insert(app.log_buffer, &end, msg, -1);
    } else if (status != 0) {
        gtk_text_buffer_insert(app.log_buffer, &end, "\n[THẤT BẠI! Xem lỗi chi tiết ở trên]\n", -1);
    }

    // Cuộn xuống cuối lần cuối
    GtkTextMark *mark = gtk_text_buffer_get_insert(app.log_buffer);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app.log_text_view), mark, 0.0, FALSE, 0.0, 0.0);
}

// Callback CLEAN - vẫn giữ nguyên, vì clean không cần verbose
static void on_clean_clicked(GtkButton *button, gpointer data) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && make clean", KERNEL_DIR);
    run_command(cmd, "=== CLEAN COMPLETED SUCCESSFULLY ===");
}

// Callback BUILD - THÊM V=1 ĐỂ HIỂN THỊ ĐẦY ĐỦ LỆNH GCC
static void on_build_clicked(GtkButton *button, gpointer data) {
    char cmd[1024];
    // V=1 là chìa khóa: buộc Kbuild hiển thị toàn bộ lệnh biên dịch
    snprintf(cmd, sizeof(cmd), 
             "cd \"%s\" && make clean && make V=1", KERNEL_DIR);
    run_command(cmd, "=== BUILD COMPLETED SUCCESSFULLY ===\nTất cả file .ko đã sẵn sàng!");
}
// 1. Hiển thị danh sách module đang load trong kernel (lsmod)
static void on_refresh_modules_clicked(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(app.log_buffer, "Đang lấy danh sách module từ kernel...\n", -1);
    run_command("lsmod | grep -E 'soft_i2c|mpu6050|ssd1306' || echo 'Không tìm thấy module nào của dự án trong kernel'", 
                "=== DANH SÁCH MODULE HIỆN TẠI TRONG KERNEL (lsmod) ===\n"
                "Cột 'Used by' chính là reference count - nếu >0 thì không rmmod được!");
}

// 2. Hiển thị thông tin chi tiết của các file .ko (modinfo)
static void on_modinfo_clicked(GtkButton *button, gpointer data) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "modinfo %s/soft_i2c.ko 2>/dev/null || echo 'soft_i2c.ko chưa build';\n"
             "modinfo %s/mpu6050.ko 2>/dev/null || echo 'mpu6050.ko chưa build';\n"
             "modinfo %s/ssd1306.ko 2>/dev/null || echo 'ssd1306.ko chưa build'",
             KERNEL_DIR, KERNEL_DIR, KERNEL_DIR);
    gtk_text_buffer_set_text(app.log_buffer, "Đang lấy thông tin module (.ko)...\n", -1);
    run_command(cmd, 
                "=== THÔNG TIN CHI TIẾT CÁC MODULE ===\n"
                "vermagic: kernel kiểm tra version + compiler flags\n"
                "depends: dependency giữa các module\n"
                "license: phải GPL để export một số symbol");
}

// 3. Hiển thị major/minor number và device node
static void on_device_info_clicked(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(app.log_buffer, "Đang kiểm tra device registration...\n", -1);
    
    // Hiển thị character devices có đăng ký mpu6050 không
    run_command("echo '=== Character devices đã đăng ký trong kernel ===' && "
                "cat /proc/devices | grep -i mpu6050 || echo 'Không tìm thấy mpu6050 trong /proc/devices'",
                NULL);
    
    // Hiển thị toàn bộ character devices để thấy major number
    run_command("echo '=== Danh sách một phần /proc/devices ===' && "
                "cat /proc/devices | grep Character -A 10",
                NULL);
    
    // Hiển thị device node /dev/mpu6050
    run_command("echo '=== Device node /dev/mpu6050 ===' && "
                "ls -l /dev/mpu6050 2>/dev/null || echo '/dev/mpu6050 chưa được tạo (module chưa load hoặc udev chưa chạy)'",
                "=== HOÀN TẤT KIỂM TRA DEVICE REGISTRATION ===\n"
                "Major number: kernel dùng để map từ device node → driver\n"
                "Device node được tạo bởi udev dựa trên major:minor");
}

// Các callback load/unload giữ nguyên như cũ
static void on_load_soft_i2c_clicked(GtkButton *button, gpointer data) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sudo insmod %s/soft_i2c.ko", KERNEL_DIR);
    run_command(cmd, "soft_i2c.ko loaded successfully!");
}

static void on_load_mpu6050_clicked(GtkButton *button, gpointer data) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sudo insmod %s/mpu6050.ko", KERNEL_DIR);
    run_command(cmd, "mpu6050.ko loaded successfully!");
}

static void on_load_ssd1306_clicked(GtkButton *button, gpointer data) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sudo insmod %s/ssd1306.ko", KERNEL_DIR);
    run_command(cmd, "ssd1306.ko loaded successfully!");
}

static void on_unload_soft_i2c_clicked(GtkButton *button, gpointer data) {
    run_command("sudo rmmod soft_i2c || true", "soft_i2c unloaded!");
}

static void on_unload_mpu6050_clicked(GtkButton *button, gpointer data) {
    run_command("sudo rmmod mpu6050 || true", "mpu6050 unloaded!");
}

static void on_unload_ssd1306_clicked(GtkButton *button, gpointer data) {
    run_command("sudo rmmod ssd1306 || true", "ssd1306 unloaded!");
}

// Các hàm còn lại (update_ui, read_sensor_data, start/stop, mode) giữ nguyên hoàn toàn
static void update_ui() {
    const double max_val = 17000.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.prog_x),
                                  (app.data.accel_x + max_val) / (2 * max_val));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.prog_y),
                                  (app.data.accel_y + max_val) / (2 * max_val));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.prog_z),
                                  (app.data.accel_z + max_val) / (2 * max_val));

    char status[256];
    snprintf(status, sizeof(status),
             "Accel X: %6d | Accel Y: %6d | Accel Z: %6d",
             app.data.accel_x, app.data.accel_y, app.data.accel_z);
    gtk_label_set_text(GTK_LABEL(app.label_status), status);

    gtk_label_set_text(GTK_LABEL(app.label_log), app.kernel_log);
}

static gboolean read_sensor_data(gpointer user_data) {
    if (!app.running) return TRUE;

    if (app.simulation_mode) {
        app.data.accel_x = (rand() % 20000) - 10000;
        app.data.accel_y = (rand() % 20000) - 10000;
        app.data.accel_z = (rand() % 20000) - 10000;
        snprintf(app.kernel_log, sizeof(app.kernel_log), "[Simulation] Random data generated");
    } else {
        if (read(app.fd, &app.data, sizeof(app.data)) < 0) {
            snprintf(app.kernel_log, sizeof(app.kernel_log), "[Error] Cannot read /dev/mpu6050 - Module not loaded?");
            app.running = 0;
        } else {
            FILE *fp = popen("dmesg | grep -i MPU6050 | tail -n 2", "r");
            if (fp) {
                app.kernel_log[0] = '\0';
                char line[512];
                while (fgets(line, sizeof(line), fp)) {
                    strncat(app.kernel_log, line, sizeof(app.kernel_log) - strlen(app.kernel_log) - 1);
                }
                pclose(fp);
            }
        }
    }
    update_ui();
    app.history_x[app.history_index] = app.data.accel_x;
    app.history_y[app.history_index] = app.data.accel_y;
    app.history_z[app.history_index] = app.data.accel_z;

    app.history_index = (app.history_index + 1) % 100;
    if (app.history_count < 100) app.history_count++;

    // Yêu cầu vẽ lại biểu đồ
    gtk_widget_queue_draw(app.drawing_area);
    return TRUE;
}

static void draw_chart(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    // Nền trắng
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Lưới ngang
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i < 5; i++) {
        double y = height * i / 5.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
        cairo_stroke(cr);
    }

    // Trục giữa (0g)
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, 0, height / 2.0);
    cairo_line_to(cr, width, height / 2.0);
    cairo_stroke(cr);

    if (app.history_count == 0) return;

    // Vẽ 3 đường: X (đỏ), Y (xanh lá), Z (xanh dương)
    double scale_y = height / 2.0 / 20000.0;  // ±17000 ~ full height
    double step_x = (double)width / 99.0;     // 100 điểm

    // Vẽ X - đỏ
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.0);
    for (int i = 0; i < app.history_count; i++) {
        int idx = (app.history_index - app.history_count + i + 100) % 100;
        double x = i * step_x;
        double y = height / 2.0 - app.history_x[idx] * scale_y;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Vẽ Y - xanh lá
    cairo_set_source_rgb(cr, 0.0, 0.8, 0.0);
    cairo_set_line_width(cr, 2.0);
    for (int i = 0; i < app.history_count; i++) {
        int idx = (app.history_index - app.history_count + i + 100) % 100;
        double x = i * step_x;
        double y = height / 2.0 - app.history_y[idx] * scale_y;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Vẽ Z - xanh dương
    cairo_set_source_rgb(cr, 0.0, 0.3, 1.0);
    cairo_set_line_width(cr, 2.0);
    for (int i = 0; i < app.history_count; i++) {
        int idx = (app.history_index - app.history_count + i + 100) % 100;
        double x = i * step_x;
        double y = height / 2.0 - app.history_z[idx] * scale_y;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Chú thích
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, 10, 20);
    cairo_show_text(cr, "X (Red)");
    cairo_move_to(cr, 70, 20);
    cairo_show_text(cr, "Y (Green)");
    cairo_move_to(cr, 150, 20);
    cairo_show_text(cr, "Z (Blue)");
}

static void on_start_clicked(GtkButton *button, gpointer data) {
    if (app.running) return;

    if (!app.simulation_mode) {
        app.fd = open(DEVICE_NODE, O_RDONLY);
        if (app.fd < 0) {
            gtk_label_set_text(GTK_LABEL(app.label_log), "[Error] /dev/mpu6050 not found. Load mpu6050.ko first!");
            return;
        }
    }

    app.running = 1;
    srand(time(NULL));
    g_timeout_add(200, read_sensor_data, NULL);

    gtk_widget_set_sensitive(app.btn_start, FALSE);
    gtk_widget_set_sensitive(app.btn_stop, TRUE);
    strcpy(app.kernel_log, "Monitoring started...");
    update_ui();
}

static void on_stop_clicked(GtkButton *button, gpointer data) {
    app.running = 0;
    if (app.fd >= 0) { close(app.fd); app.fd = -1; }
    gtk_widget_set_sensitive(app.btn_start, TRUE);
    gtk_widget_set_sensitive(app.btn_stop, FALSE);
    strcpy(app.kernel_log, "Monitoring stopped.");
    update_ui();
}

static void on_mode_toggled(GtkCheckButton *check, gpointer data) {
    if (gtk_check_button_get_active(check)) {
        app.simulation_mode = GPOINTER_TO_INT(data);
    }
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    app.window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app.window), "MPU6050 Monitor - Full Control");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 850, 750);  // Tăng chiều cao để có chỗ cho log

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(app.window), vbox);

    // Tiêu đề
    GtkWidget *title = gtk_label_new("<b><span size='xx-large'>MPU6050 Sensor Monitor</span></b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(vbox), title);

    // Chọn mode
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_append(GTK_BOX(vbox), mode_box);

    GtkWidget *radio_real = gtk_check_button_new_with_label("Real Hardware");
    GtkWidget *radio_sim = gtk_check_button_new_with_label("Simulation Mode");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(radio_sim), GTK_CHECK_BUTTON(radio_real));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio_real), TRUE);
    g_signal_connect(radio_real, "toggled", G_CALLBACK(on_mode_toggled), GINT_TO_POINTER(0));
    g_signal_connect(radio_sim, "toggled", G_CALLBACK(on_mode_toggled), GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(mode_box), radio_real);
    gtk_box_append(GTK_BOX(mode_box), radio_sim);

    // Nút Clean & Build
    GtkWidget *build_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(vbox), build_box);

    app.btn_clean = gtk_button_new_with_label("CLEAN ALL MODULES");
    app.btn_build = gtk_button_new_with_label("BUILD ALL MODULES");
    app.btn_clear_log = gtk_button_new_with_label("CLEAR LOG");
    gtk_box_append(GTK_BOX(build_box), app.btn_clean);
    gtk_box_append(GTK_BOX(build_box), app.btn_build);
    gtk_box_append(GTK_BOX(build_box), app.btn_clear_log);
    g_signal_connect(app.btn_clean, "clicked", G_CALLBACK(on_clean_clicked), NULL);
    g_signal_connect(app.btn_build, "clicked", G_CALLBACK(on_build_clicked), NULL);
    g_signal_connect(app.btn_clear_log, "clicked", G_CALLBACK(on_clear_log_clicked), NULL);
    
    GtkWidget *btn_refresh_modules = gtk_button_new_with_label("REFRESH MODULE LIST");
    GtkWidget *btn_modinfo = gtk_button_new_with_label("MODULE INFO");
    GtkWidget *btn_device_info = gtk_button_new_with_label("DEVICE INFO");

    gtk_box_append(GTK_BOX(build_box), btn_refresh_modules);
    gtk_box_append(GTK_BOX(build_box), btn_modinfo);
    gtk_box_append(GTK_BOX(build_box), btn_device_info);

    g_signal_connect(btn_refresh_modules, "clicked", G_CALLBACK(on_refresh_modules_clicked), NULL);
    g_signal_connect(btn_modinfo, "clicked", G_CALLBACK(on_modinfo_clicked), NULL);
    g_signal_connect(btn_device_info, "clicked", G_CALLBACK(on_device_info_clicked), NULL);

    // Bảng Load/Unload từng module
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_box_append(GTK_BOX(vbox), grid);

    app.btn_load_soft_i2c   = gtk_button_new_with_label("Load soft_i2c");
    app.btn_load_mpu6050    = gtk_button_new_with_label("Load mpu6050");
    app.btn_load_ssd1306    = gtk_button_new_with_label("Load ssd1306");
    app.btn_unload_soft_i2c = gtk_button_new_with_label("Unload soft_i2c");
    app.btn_unload_mpu6050  = gtk_button_new_with_label("Unload mpu6050");
    app.btn_unload_ssd1306  = gtk_button_new_with_label("Unload ssd1306");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("MODULE"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("LOAD"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("UNLOAD"), 2, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("soft_i2c"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_load_soft_i2c, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_unload_soft_i2c, 2, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("mpu6050"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_load_mpu6050, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_unload_mpu6050, 2, 2, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("ssd1306"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_load_ssd1306, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_unload_ssd1306, 2, 3, 1, 1);

    g_signal_connect(app.btn_load_soft_i2c,   "clicked", G_CALLBACK(on_load_soft_i2c_clicked), NULL);
    g_signal_connect(app.btn_load_mpu6050,    "clicked", G_CALLBACK(on_load_mpu6050_clicked), NULL);
    g_signal_connect(app.btn_load_ssd1306,    "clicked", G_CALLBACK(on_load_ssd1306_clicked), NULL);
    g_signal_connect(app.btn_unload_soft_i2c, "clicked", G_CALLBACK(on_unload_soft_i2c_clicked), NULL);
    g_signal_connect(app.btn_unload_mpu6050,  "clicked", G_CALLBACK(on_unload_mpu6050_clicked), NULL);
    g_signal_connect(app.btn_unload_ssd1306,  "clicked", G_CALLBACK(on_unload_ssd1306_clicked), NULL);

    // Nút Start / Stop
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    app.btn_start = gtk_button_new_with_label("START MONITORING");
    app.btn_stop  = gtk_button_new_with_label("STOP");
    gtk_widget_set_sensitive(app.btn_stop, FALSE);
    g_signal_connect(app.btn_start, "clicked", G_CALLBACK(on_start_clicked), NULL);
    g_signal_connect(app.btn_stop,  "clicked", G_CALLBACK(on_stop_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), app.btn_start);
    gtk_box_append(GTK_BOX(btn_box), app.btn_stop);

    // === PHẦN HIỂN THỊ DỮ LIỆU CẢM BIẾN (QUAN TRỌNG - ĐÃ THÊM LẠI) ===
    app.prog_x = gtk_progress_bar_new();
    app.prog_y = gtk_progress_bar_new();
    app.prog_z = gtk_progress_bar_new();

    GtkWidget *pb_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pb_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(pb_grid), 10);
    gtk_box_append(GTK_BOX(vbox), pb_grid);

    gtk_grid_attach(GTK_GRID(pb_grid), gtk_label_new("Accel X:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), app.prog_x, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), gtk_label_new("Accel Y:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), app.prog_y, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), gtk_label_new("Accel Z:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), app.prog_z, 1, 2, 1, 1);

    GtkWidget *chart_frame = gtk_frame_new("Realtime Accelerometer Chart (Last ~20s)");
    gtk_box_append(GTK_BOX(vbox), chart_frame);

    app.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.drawing_area, 800, 200);
    gtk_frame_set_child(GTK_FRAME(chart_frame), app.drawing_area);

    // Kết nối hàm vẽ
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.drawing_area),
                                   draw_chart, NULL, NULL);

    // Label hiển thị giá trị số
    app.label_status = gtk_label_new("Ready - Press START to begin monitoring");
    gtk_label_set_selectable(GTK_LABEL(app.label_status), TRUE);
    gtk_box_append(GTK_BOX(vbox), app.label_status);

    // === PHẦN LOG CUỘN (ĐẶT Ở DƯỚI CÙNG, CHIẾM NHIỀU KHÔNG GIAN) ===
    GtkWidget *log_frame = gtk_frame_new("Kernel & Build Log");
    gtk_box_append(GTK_BOX(vbox), log_frame);

    app.log_scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app.log_scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(app.log_scrolled_window), 250);
    gtk_widget_set_vexpand(app.log_scrolled_window, TRUE);  // Tự giãn theo cửa sổ
    gtk_frame_set_child(GTK_FRAME(log_frame), app.log_scrolled_window);

    app.log_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app.log_text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app.log_text_view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app.log_text_view), FALSE);

    app.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.log_text_view));
    gtk_text_buffer_set_text(app.log_buffer, 
        "=== MPU6050 Monitor Ready ===\n"
        "Sử dụng các nút trên để build/load module.\n"
        "Log sẽ hiện realtime tại đây.\n\n", -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app.log_scrolled_window), app.log_text_view);

    // Khởi tạo trạng thái ban đầu
    app.fd = -1;
    app.running = 0;
    app.simulation_mode = 0;
    app.history_index = 0;
    app.history_count = 0;
    memset(app.history_x, 0, sizeof(app.history_x));
    memset(app.history_y, 0, sizeof(app.history_y));
    memset(app.history_z, 0, sizeof(app.history_z));

    gtk_window_present(GTK_WINDOW(app.window));
}

int main(int argc, char **argv) {
    GtkApplication *gtk_app = gtk_application_new("vn.mpu6050.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}