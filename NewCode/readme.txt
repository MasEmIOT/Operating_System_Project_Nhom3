1. Hướng dẫn Biên dịch & Cài đặt

Bước 1: Chuẩn bị môi trường
Cài đặt Kernel Headers và công cụ biên dịch:
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
Bước 2: Cấu hình Chế độ (Simulation vs Hardware)
Mở file soft_i2c.c, sửa dòng #define:
#define SIMULATION_MODE 1: Để chạy thử trên máy tính (PC).
#define SIMULATION_MODE 0: Để chạy thật trên Raspberry Pi (Hardware).
Bước 3: Biên dịch
Tại thư mục dự án, chạy:
make clean
make
Bước 4: Nạp Modules
# 1. Nạp Core trước
sudo insmod soft_i2c.ko
# 2. Nạp Drivers thiết bị
sudo insmod mpu6050.ko
sudo insmod ssd1306.ko
Chạy Ứng dụng: sudo ./monitor_app  
2. Gỡ cài đặt
Để dọn dẹp hệ thống sau khi demo:
sudo rmmod ssd1306
sudo rmmod mpu6050
sudo rmmod soft_i2c
make clean
