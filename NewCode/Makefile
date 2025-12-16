# Makefile Full Option cho Đồ án Hệ điều hành
# Biên dịch 3 module Kernel và 1 User App

# 1. Danh sách các module cần biên dịch (tên phải khớp với file .c)
obj-m += soft_i2c.o
obj-m += mpu6050.o
obj-m += ssd1306.o

# 2. Đường dẫn đến Kernel Headers hiện tại
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# 3. Target mặc định (chạy khi gõ 'make')
all:
	@echo "--- [1/2] Đang biên dịch Kernel Modules (.ko) ---"
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	
	@echo "--- [2/2] Đang biên dịch User Application (monitor_app) ---"
	gcc -o monitor_app monitor_app.c -lpthread -lm
	
	@echo "==============================================="
	@echo ">> HOÀN TẤT! Đã tạo ra các file:"
	@echo "   1. soft_i2c.ko   (Module I2C Core)"
	@echo "   2. mpu6050.ko    (Module Driver Cảm biến)"
	@echo "   3. ssd1306.ko    (Module Driver Màn hình)"
	@echo "   4. monitor_app   (Ứng dụng người dùng)"
	@echo "==============================================="

# 4. Target dọn dẹp file rác (chạy khi gõ 'make clean')
clean:
	@echo "--- Đang dọn dẹp file rác ---"
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f monitor_app
	@echo ">> Đã xóa sạch!"
