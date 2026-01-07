# Operating_System_Project_Nhom3
Members: Nguyen Tien Dat (leader), Hoang Tuan Hung, Nguyen Xuan Hieu, Vu Tien Dat, Nguyen Ho Trieu Duong

#MPU6050
cd ~mpu_project/kernel
make
sudo insmod mpu6050_kmod.ko
dmesg
cd cd ~mpu_project/user
make
sudo ./mpu_monitor
q
sudo rmmod mpu6050_kmod

#OLED SSD1306
cd ~oled
make
sudo insmod ssd1306_i2c.ko
dmesg
gcc -O2 -o test_ssd1306_write test_ssd1306_write.c -lm
sudo ./test_ssd1306_write



static void activate(GtkApplication *gtk_app, gpointer user_data) {
    app.window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app.window), "MPU6050 Monitor");
    // GIẢM KÍCH THƯỚC CỬA SỔ ĐỂ VỪA MÀN HÌNH PI NHỎ
    gtk_window_set_default_size(GTK_WINDOW(app.window), 780, 620);  // Giảm từ 850x750 xuống
    gtk_window_set_resizable(GTK_WINDOW(app.window), FALSE);        // Không cho resize, tránh lệch

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);     // Giảm spacing từ 15 → 8
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_window_set_child(GTK_WINDOW(app.window), vbox);

    // Tiêu đề nhỏ hơn
    GtkWidget *title = gtk_label_new("<b><span size='large'>MPU6050 Monitor</span></b>");
    gtk_label_set_use_markup(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(vbox), title);

    // Mode chọn - nhỏ gọn hơn
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

    // Nút Build/Clean - thu gọn lại
    GtkWidget *build_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(vbox), build_box);

    app.btn_clean = gtk_button_new_with_label("Clean");
    app.btn_build = gtk_button_new_with_label("Build");
    app.btn_clear_log = gtk_button_new_with_label("Clear Log");

    GtkWidget *btn_refresh_modules = gtk_button_new_with_label("Modules");
    GtkWidget *btn_modinfo = gtk_button_new_with_label("Info");
    GtkWidget *btn_device_info = gtk_button_new_with_label("Device");

    gtk_box_append(GTK_BOX(build_box), app.btn_clean);
    gtk_box_append(GTK_BOX(build_box), app.btn_build);
    gtk_box_append(GTK_BOX(build_box), app.btn_clear_log);
    gtk_box_append(GTK_BOX(build_box), btn_refresh_modules);
    gtk_box_append(GTK_BOX(build_box), btn_modinfo);
    gtk_box_append(GTK_BOX(build_box), btn_device_info);

    g_signal_connect(app.btn_clean, "clicked", G_CALLBACK(on_clean_clicked), NULL);
    g_signal_connect(app.btn_build, "clicked", G_CALLBACK(on_build_clicked), NULL);
    g_signal_connect(app.btn_clear_log, "clicked", G_CALLBACK(on_clear_log_clicked), NULL);
    g_signal_connect(btn_refresh_modules, "clicked", G_CALLBACK(on_refresh_modules_clicked), NULL);
    g_signal_connect(btn_modinfo, "clicked", G_CALLBACK(on_modinfo_clicked), NULL);
    g_signal_connect(btn_device_info, "clicked", G_CALLBACK(on_device_info_clicked), NULL);

    // Load/Unload module - dùng grid gọn hơn
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

    // Kết nối signal như cũ
    g_signal_connect(app.btn_load_soft_i2c,   "clicked", G_CALLBACK(on_load_soft_i2c_clicked), NULL);
    g_signal_connect(app.btn_load_mpu6050,    "clicked", G_CALLBACK(on_load_mpu6050_clicked), NULL);
    g_signal_connect(app.btn_load_ssd1306,    "clicked", G_CALLBACK(on_load_ssd1306_clicked), NULL);
    g_signal_connect(app.btn_unload_soft_i2c, "clicked", G_CALLBACK(on_unload_soft_i2c_clicked), NULL);
    g_signal_connect(app.btn_unload_mpu6050,  "clicked", G_CALLBACK(on_unload_mpu6050_clicked), NULL);
    g_signal_connect(app.btn_unload_ssd1306,  "clicked", G_CALLBACK(on_unload_ssd1306_clicked), NULL);

    // Start / Stop
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    app.btn_start = gtk_button_new_with_label("START");
    app.btn_stop  = gtk_button_new_with_label("STOP");
    gtk_widget_set_sensitive(app.btn_stop, FALSE);
    g_signal_connect(app.btn_start, "clicked", G_CALLBACK(on_start_clicked), NULL);
    g_signal_connect(app.btn_stop,  "clicked", G_CALLBACK(on_stop_clicked), NULL);
    gtk_box_append(GTK_BOX(btn_box), app.btn_start);
    gtk_box_append(GTK_BOX(btn_box), app.btn_stop);

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

    // Biểu đồ - giảm chiều cao xuống
    GtkWidget *chart_frame = gtk_frame_new("Chart (last ~20s)");
    gtk_box_append(GTK_BOX(vbox), chart_frame);

    app.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.drawing_area, 740, 150);  // Giảm từ 800x200 → 740x150
    gtk_frame_set_child(GTK_FRAME(chart_frame), app.drawing_area);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.drawing_area), draw_chart, NULL, NULL);

    // Status label
    app.label_status = gtk_label_new("Ready - Press START");
    gtk_label_set_selectable(GTK_LABEL(app.label_status), TRUE);
    gtk_box_append(GTK_BOX(vbox), app.label_status);

    // Log area - giảm chiều cao, vẫn cuộn được
    GtkWidget *log_frame = gtk_frame_new("Log");
    gtk_box_append(GTK_BOX(vbox), log_frame);

    app.log_scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app.log_scrolled_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(app.log_scrolled_window), 180); // Giảm từ 250
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

    // Khởi tạo biến
    app.fd = -1;
    app.running = 0;
    app.simulation_mode = 0;
    app.history_index = 0;
    app.history_count = 0;

    gtk_window_present(GTK_WINDOW(app.window));
}
