#include <gtk/gtk.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define DEVICE_NODE "/dev/mpu6050"
#define KERNEL_DIR ".."  // Nếu kernel module nằm ở thư mục cha thì để ".."

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
    GtkWidget *prog_x, *prog_y, *prog_z;
    GtkWidget *btn_start, *btn_stop;

    // Nút quản lý module
    GtkWidget *btn_build;
    GtkWidget *btn_clean;
    GtkWidget *btn_load_soft_i2c;
    GtkWidget *btn_load_mpu6050;
    GtkWidget *btn_load_ssd1306;
    GtkWidget *btn_unload_soft_i2c;
    GtkWidget *btn_unload_mpu6050;
    GtkWidget *btn_unload_ssd1306;
    GtkWidget *btn_clear_log;

    // Nút kiểm tra module 
    GtkWidget *btn_refresh_modules;
    GtkWidget *btn_modinfo;
    GtkWidget *btn_device_info;

    //  ACC / GYRO
    GtkWidget *btn_mode_acc;
    GtkWidget *btn_mode_gyro;

    GtkWidget *log_scrolled_window;
    GtkWidget *log_text_view;
    GtkTextBuffer *log_buffer;

    int fd;
    int running;
    int simulation_mode;
    int display_mode;  // 0 = ACC, 1 = GYRO

    struct mpu_data data;
    char kernel_log[1024];

    GtkWidget *drawing_area;
    GtkWidget *chart_frame;

    // Lịch sử riêng cho từng chế độ
    double history_acc_x[100], history_acc_y[100], history_acc_z[100];
    double history_gyro_x[100], history_gyro_y[100], history_gyro_z[100];
    int history_index;
    int history_count;
} App;

static App app = {0};

// Cap NHat UI
static void update_ui() {
    if (!GTK_IS_LABEL(app.label_status)) return;

    const double max_val = 17000.0;
    const char *prefix = app.display_mode == 0 ? "Acc" : "Gyr";
    short vx = app.display_mode == 0 ? app.data.accel_x : app.data.gyro_x;
    short vy = app.display_mode == 0 ? app.data.accel_y : app.data.gyro_y;
    short vz = app.display_mode == 0 ? app.data.accel_z : app.data.gyro_z;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.prog_x), (vx + max_val) / (2 * max_val));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.prog_y), (vy + max_val) / (2 * max_val));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app.prog_z), (vz + max_val) / (2 * max_val));

    char status[256];
    snprintf(status, sizeof(status), "%s X: %6d | %s Y: %6d | %s Z: %6d",
             prefix, vx, prefix, vy, prefix, vz);
    gtk_label_set_text(GTK_LABEL(app.label_status), status);
}

// CHUYEN CHE DO DO VA QUAN SAT
static void on_display_mode_toggled(GtkCheckButton *check, gpointer data) {
    if (gtk_check_button_get_active(check)) {
        app.display_mode = GPOINTER_TO_INT(data);
        const char *title = (app.display_mode == 0) ? "Chart ACC (last ~20s)" : "Chart GYRO (last ~20s)";
        gtk_frame_set_label(GTK_FRAME(app.chart_frame), title);
        update_ui();
        gtk_widget_queue_draw(app.drawing_area);
    }
}

// ==================== CLEAR LOG ====================
static void on_clear_log_clicked(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(app.log_buffer, "=== LOG CLEARED ===\nSẵn sàng cho hành động mới!\n\n", -1);
}

// ==================== RUN COMMAND (realtime log) ====================
static void run_command(const char *cmd, const char *success_msg) {
    char full_cmd[2048];
    snprintf(full_cmd, sizeof(full_cmd), "/bin/bash -c \"%s 2>&1\"", cmd);

    gtk_text_buffer_set_text(app.log_buffer, "Đang thực thi lệnh...\nVui lòng chờ (có thể mất vài giây)...\n\n", -1);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        gtk_text_buffer_set_text(app.log_buffer, "Lỗi: Không thể thực thi lệnh!\n", -1);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(app.log_buffer, &end);
        gtk_text_buffer_insert(app.log_buffer, &end, line, -1);
        gtk_text_buffer_insert(app.log_buffer, &end, "\n", -1);

        GtkTextMark *mark = gtk_text_buffer_get_insert(app.log_buffer);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app.log_text_view), mark, 0.0, FALSE, 0.0, 0.0);

        GMainContext *context = g_main_context_default();
        while (g_main_context_iteration(context, FALSE));
    }

    int status = pclose(fp);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(app.log_buffer, &end);

    if (status == 0 && success_msg) {
        char msg[512];
        snprintf(msg, sizeof(msg), "\n%s\n", success_msg);
        gtk_text_buffer_insert(app.log_buffer, &end, msg, -1);
    } else if (status != 0) {
        gtk_text_buffer_insert(app.log_buffer, &end, "\n[THẤT BẠI! Xem lỗi chi tiết ở trên]\n", -1);
    }

    GtkTextMark *mark = gtk_text_buffer_get_insert(app.log_buffer);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app.log_text_view), mark, 0.0, FALSE, 0.0, 0.0);
}

// MODULE CONTROL 
static void on_clean_clicked(GtkButton *button, gpointer data) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && make clean", KERNEL_DIR);
    run_command(cmd, "=== CLEAN COMPLETED SUCCESSFULLY ===");
}

static void on_build_clicked(GtkButton *button, gpointer data) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd \"%s\" && make clean && make V=1", KERNEL_DIR);
    run_command(cmd, "=== BUILD COMPLETED SUCCESSFULLY ===\nTất cả file .ko đã sẵn sàng!");
}

static void on_refresh_modules_clicked(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(app.log_buffer, "Đang lấy danh sách module từ kernel...\n", -1);
    run_command("lsmod | grep -E 'soft_i2c|mpu6050|ssd1306' || echo 'Không tìm thấy module nào của dự án trong kernel'", 
                "=== DANH SÁCH MODULE HIỆN TẠI TRONG KERNEL (lsmod) ===\n"
                "Cột 'Used by' chính là reference count - nếu >0 thì không rmmod được!");
}

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

static void on_device_info_clicked(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(app.log_buffer, "Đang kiểm tra device registration...\n", -1);
    
    run_command("echo '=== Character devices đã đăng ký trong kernel ===' && "
                "cat /proc/devices | grep -i mpu6050 || echo 'Không tìm thấy mpu6050 trong /proc/devices'",
                NULL);
    
    run_command("echo '=== Danh sách một phần /proc/devices ===' && "
                "cat /proc/devices | grep Character -A 10",
                NULL);
    
    run_command("echo '=== Device node /dev/mpu6050 ===' && "
                "ls -l /dev/mpu6050 2>/dev/null || echo '/dev/mpu6050 chưa được tạo (module chưa load hoặc udev chưa chạy)'",
                "=== HOÀN TẤT KIỂM TRA DEVICE REGISTRATION ===\n"
                "Major number: kernel dùng để map từ device node → driver\n"
                "Device node được tạo bởi udev dựa trên major:minor");
}

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

// DATA READ
static gboolean read_sensor_data(gpointer user_data) {
    if (!app.running) return TRUE;

    if (app.simulation_mode) {
        app.data.accel_x = (rand() % 20000) - 10000;
        app.data.accel_y = (rand() % 20000) - 10000;
        app.data.accel_z = (rand() % 20000) - 10000;
        app.data.gyro_x  = (rand() % 20000) - 10000;
        app.data.gyro_y  = (rand() % 20000) - 10000;
        app.data.gyro_z  = (rand() % 20000) - 10000;
        snprintf(app.kernel_log, sizeof(app.kernel_log), "[Simulation] Random data generated");
    } else {
        if (read(app.fd, &app.data, sizeof(app.data)) < 0) {
            snprintf(app.kernel_log, sizeof(app.kernel_log), "[Error] Cannot read /dev/mpu6050 - Module not loaded?");
            app.running = 0;
        } else {
            app.kernel_log[0] = '\0';  // Có thể lấy dmesg nếu cần
        }
    }

    // Lưu lịch sử theo chế độ hiện tại
    if (app.display_mode == 0) {
        app.history_acc_x[app.history_index] = app.data.accel_x;
        app.history_acc_y[app.history_index] = app.data.accel_y;
        app.history_acc_z[app.history_index] = app.data.accel_z;
    } else {
        app.history_gyro_x[app.history_index] = app.data.gyro_x;
        app.history_gyro_y[app.history_index] = app.data.gyro_y;
        app.history_gyro_z[app.history_index] = app.data.gyro_z;
    }

    app.history_index = (app.history_index + 1) % 100;
    if (app.history_count < 100) app.history_count++;

    update_ui();
    gtk_widget_queue_draw(app.drawing_area);
    return TRUE;
}

// GRAPH 
static void draw_chart(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i < 5; i++) {
        double y = height * i / 5.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
        cairo_stroke(cr);
    }

    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, 0, height / 2.0);
    cairo_line_to(cr, width, height / 2.0);
    cairo_stroke(cr);

    if (app.history_count == 0) return;

    double *hx = app.display_mode == 0 ? app.history_acc_x : app.history_gyro_x;
    double *hy = app.display_mode == 0 ? app.history_acc_y : app.history_gyro_y;
    double *hz = app.display_mode == 0 ? app.history_acc_z : app.history_gyro_z;

    double scale_y = height / 2.0 / 20000.0;
    double step_x = (double)width / 99.0;

    // X - đỏ
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_set_line_width(cr, 2.0);
    for (int i = 0; i < app.history_count; i++) {
        int idx = (app.history_index - app.history_count + i + 100) % 100;
        double x = i * step_x;
        double y = height / 2.0 - hx[idx] * scale_y;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Y - xanh lá
    cairo_set_source_rgb(cr, 0.0, 0.8, 0.0);
    for (int i = 0; i < app.history_count; i++) {
        int idx = (app.history_index - app.history_count + i + 100) % 100;
        double x = i * step_x;
        double y = height / 2.0 - hy[idx] * scale_y;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    // Z - xanh dương
    cairo_set_source_rgb(cr, 0.0, 0.3, 1.0);
    for (int i = 0; i < app.history_count; i++) {
        int idx = (app.history_index - app.history_count + i + 100) % 100;
        double x = i * step_x;
        double y = height / 2.0 - hz[idx] * scale_y;
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

// ==================== START / STOP ====================
static void on_start_clicked(GtkButton *button, gpointer data) {
    if (app.running) return;

    if (!app.simulation_mode) {
        app.fd = open(DEVICE_NODE, O_RDONLY);
        if (app.fd < 0) {
            gtk_label_set_text(GTK_LABEL(app.label_status), "[Error] /dev/mpu6050 not found. Load mpu6050.ko first!");
            return;
        }
    }

    app.running = 1;
    app.history_index = 0;
    app.history_count = 0;
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

// INTERFACE
static void activate(GtkApplication *gtk_app, gpointer user_data) {
    app.window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app.window), "MPU6050 Monitor");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 780, 620);
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_window_set_child(GTK_WINDOW(app.window), vbox);

    GtkWidget *title = gtk_label_new("<b><span size='large'>MPU6050 Monitor</span></b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(vbox), title);

    // Real / Sim
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(vbox), mode_box);

    GtkWidget *radio_real = gtk_check_button_new_with_label("Real");
    GtkWidget *radio_sim = gtk_check_button_new_with_label("Sim");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(radio_sim), GTK_CHECK_BUTTON(radio_real));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio_real), TRUE);
    g_signal_connect(radio_real, "toggled", G_CALLBACK(on_mode_toggled), GINT_TO_POINTER(0));
    g_signal_connect(radio_sim, "toggled", G_CALLBACK(on_mode_toggled), GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(mode_box), radio_real);
    gtk_box_append(GTK_BOX(mode_box), radio_sim);

    // Build / Clean / Clear Log / Modules / Info / Device
    GtkWidget *build_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(vbox), build_box);

    app.btn_clean = gtk_button_new_with_label("Clean");
    app.btn_build = gtk_button_new_with_label("Build");
    app.btn_clear_log = gtk_button_new_with_label("Clear Log");
    app.btn_refresh_modules = gtk_button_new_with_label("Modules");
    app.btn_modinfo = gtk_button_new_with_label("Info");
    app.btn_device_info = gtk_button_new_with_label("Device");

    gtk_box_append(GTK_BOX(build_box), app.btn_clean);
    gtk_box_append(GTK_BOX(build_box), app.btn_build);
    gtk_box_append(GTK_BOX(build_box), app.btn_clear_log);
    gtk_box_append(GTK_BOX(build_box), app.btn_refresh_modules);
    gtk_box_append(GTK_BOX(build_box), app.btn_modinfo);
    gtk_box_append(GTK_BOX(build_box), app.btn_device_info);

    g_signal_connect(app.btn_clean, "clicked", G_CALLBACK(on_clean_clicked), NULL);
    g_signal_connect(app.btn_build, "clicked", G_CALLBACK(on_build_clicked), NULL);
    g_signal_connect(app.btn_clear_log, "clicked", G_CALLBACK(on_clear_log_clicked), NULL);
    g_signal_connect(app.btn_refresh_modules, "clicked", G_CALLBACK(on_refresh_modules_clicked), NULL);
    g_signal_connect(app.btn_modinfo, "clicked", G_CALLBACK(on_modinfo_clicked), NULL);
    g_signal_connect(app.btn_device_info, "clicked", G_CALLBACK(on_device_info_clicked), NULL);

    // Load/Unload grid
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_append(GTK_BOX(vbox), grid);

    app.btn_load_soft_i2c   = gtk_button_new_with_label("↑ i2c");
    app.btn_load_mpu6050    = gtk_button_new_with_label("↑ mpu");
    app.btn_load_ssd1306    = gtk_button_new_with_label("↑ oled");
    app.btn_unload_soft_i2c = gtk_button_new_with_label("↓ i2c");
    app.btn_unload_mpu6050  = gtk_button_new_with_label("↓ mpu");
    app.btn_unload_ssd1306  = gtk_button_new_with_label("↓ oled");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("soft_i2c"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_load_soft_i2c, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_unload_soft_i2c, 2, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("mpu6050"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_load_mpu6050, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_unload_mpu6050, 2, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("ssd1306"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_load_ssd1306, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.btn_unload_ssd1306, 2, 2, 1, 1);

    g_signal_connect(app.btn_load_soft_i2c,   "clicked", G_CALLBACK(on_load_soft_i2c_clicked), NULL);
    g_signal_connect(app.btn_load_mpu6050,    "clicked", G_CALLBACK(on_load_mpu6050_clicked), NULL);
    g_signal_connect(app.btn_load_ssd1306,    "clicked", G_CALLBACK(on_load_ssd1306_clicked), NULL);
    g_signal_connect(app.btn_unload_soft_i2c, "clicked", G_CALLBACK(on_unload_soft_i2c_clicked), NULL);
    g_signal_connect(app.btn_unload_mpu6050,  "clicked", G_CALLBACK(on_unload_mpu6050_clicked), NULL);
    g_signal_connect(app.btn_unload_ssd1306,  "clicked", G_CALLBACK(on_unload_ssd1306_clicked), NULL);

    // Hàng điều khiển: ACC | GYRO | START | STOP
    GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_append(GTK_BOX(vbox), control_box);

    app.btn_mode_acc = gtk_check_button_new_with_label("ACC");
    app.btn_mode_gyro = gtk_check_button_new_with_label("GYRO");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(app.btn_mode_gyro), GTK_CHECK_BUTTON(app.btn_mode_acc));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app.btn_mode_acc), TRUE);
    gtk_widget_set_size_request(app.btn_mode_acc, 100, 45);
    gtk_widget_set_size_request(app.btn_mode_gyro, 100, 45);

    g_signal_connect(app.btn_mode_acc, "toggled", G_CALLBACK(on_display_mode_toggled), GINT_TO_POINTER(0));
    g_signal_connect(app.btn_mode_gyro, "toggled", G_CALLBACK(on_display_mode_toggled), GINT_TO_POINTER(1));

    gtk_box_append(GTK_BOX(control_box), app.btn_mode_acc);
    gtk_box_append(GTK_BOX(control_box), app.btn_mode_gyro);

    app.btn_start = gtk_button_new_with_label("START");
    app.btn_stop  = gtk_button_new_with_label("STOP");
    gtk_widget_set_sensitive(app.btn_stop, FALSE);
    g_signal_connect(app.btn_start, "clicked", G_CALLBACK(on_start_clicked), NULL);
    g_signal_connect(app.btn_stop,  "clicked", G_CALLBACK(on_stop_clicked), NULL);
    gtk_box_append(GTK_BOX(control_box), app.btn_start);
    gtk_box_append(GTK_BOX(control_box), app.btn_stop);

    // Progress bars
    app.prog_x = gtk_progress_bar_new();
    app.prog_y = gtk_progress_bar_new();
    app.prog_z = gtk_progress_bar_new();

    GtkWidget *pb_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(pb_grid), 8);
    gtk_box_append(GTK_BOX(vbox), pb_grid);

    gtk_grid_attach(GTK_GRID(pb_grid), gtk_label_new("X:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), app.prog_x, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), gtk_label_new("Y:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), app.prog_y, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), gtk_label_new("Z:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(pb_grid), app.prog_z, 1, 2, 1, 1);

    // Biểu đồ
    app.chart_frame = gtk_frame_new("Chart ACC (last ~20s)");
    gtk_box_append(GTK_BOX(vbox), app.chart_frame);

    app.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.drawing_area, 740, 150);
    gtk_frame_set_child(GTK_FRAME(app.chart_frame), app.drawing_area);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.drawing_area), draw_chart, NULL, NULL);

    // Status label
    app.label_status = gtk_label_new("Ready - Press START");
    gtk_label_set_selectable(GTK_LABEL(app.label_status), TRUE);
    gtk_box_append(GTK_BOX(vbox), app.label_status);

    // Log area
    GtkWidget *log_frame = gtk_frame_new("Log");
    gtk_box_append(GTK_BOX(vbox), log_frame);

    app.log_scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app.log_scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(app.log_scrolled_window), 180);
    gtk_widget_set_vexpand(app.log_scrolled_window, TRUE);
    gtk_frame_set_child(GTK_FRAME(log_frame), app.log_scrolled_window);

    app.log_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app.log_text_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app.log_text_view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app.log_text_view), FALSE);

    app.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.log_text_view));
    gtk_text_buffer_set_text(app.log_buffer, 
        "=== MPU6050 Monitor Ready ===\n"
        "Build → Load → START\n\n", -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app.log_scrolled_window), app.log_text_view);

    // Khởi tạo
    app.fd = -1;
    app.running = 0;
    app.simulation_mode = 0;
    app.display_mode = 0;
    app.history_index = 0;
    app.history_count = 0;

    gtk_window_present(GTK_WINDOW(app.window));
}

int main(int argc, char **argv) {
    GtkApplication *gtk_app = gtk_application_new("vn.mpu6050.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
