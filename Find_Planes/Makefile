# 自动获取 Homebrew 的安装路径
HOMEBREW_PREFIX := $(shell brew --prefix)

# 编译器和编译选项
CXX := g++
CXXFLAGS := -std=c++17 -Wall -g -I$(HOMEBREW_PREFIX)/include

# 链接器选项
LDFLAGS := -L$(HOMEBREW_PREFIX)/lib -lsfml-graphics -lsfml-window -lsfml-system

# 最终生成的可执行文件名
TARGET := SDAA
# 源文件名
SRC := main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)