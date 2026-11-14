# =========================================================================
#  mihomo-tui —— mihomo 内核的终端管理界面
#
#    make            构建 build/mihomo-tui
#    make run        构建并运行 TUI
#    make demo       构建并运行 --demo-layout（假数据，不连内核）
#    make test       组件单测 + snapshot golden 回归
#    make check      公共头逐个 -fsyntax-only
#    make clean
#
#  依赖：libcurl（HTTP + NDJSON 流）、nlohmann/json（头文件）
#  颜色 / 输入 / 帧缓冲来自本仓库内置的 vaterm + vatui 子集，零外部依赖。
# =========================================================================
SHELL   := bash
CXX     ?= g++
CXXSTD  := -std=c++23
WARN    := -Wall -Wextra -Wpedantic
HARDWARN := $(WARN) -Werror
CXXFLAGS ?= -O2 -g
INCFLAGS := -Iinclude -Iapp
DEPFLAGS := -MMD -MP

BUILD   := build
BIN     := $(BUILD)/mihomo-tui

LIB_SRCS := $(shell find src -name '*.cpp' | sort)
LIB_OBJS := $(patsubst src/%.cpp,$(BUILD)/obj/src/%.o,$(LIB_SRCS))
APP_SRCS := $(shell find app -name '*.cpp' | sort)
APP_OBJS := $(patsubst app/%.cpp,$(BUILD)/obj/app/%.o,$(APP_SRCS))

LDLIBS  := -lcurl -lpthread

TEST_TARGETS := $(BUILD)/test/test_widgets $(BUILD)/test/test_use $(BUILD)/test/test_logfmt

.PHONY: all run demo test check clean fmt

all: $(BIN)

$(BIN): $(APP_OBJS) $(BUILD)/lib/libmihomo_tui.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(WARN) $(APP_OBJS) $(BUILD)/lib/libmihomo_tui.a $(LDLIBS) -o $@
	@echo "  已生成 $@"

$(BUILD)/lib/libmihomo_tui.a: $(LIB_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
	@echo "  已生成 $@"

$(BUILD)/obj/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(HARDWARN) $(CXXFLAGS) $(INCFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/obj/app/%.o: app/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(HARDWARN) $(CXXFLAGS) $(INCFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(LIB_OBJS:.o=.d) $(APP_OBJS:.o=.d)

#-- 测试 ----------------------------------------------------------------

$(BUILD)/test/test_widgets: test/test_widgets.cpp $(BUILD)/lib/libmihomo_tui.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(HARDWARN) $(CXXFLAGS) $(INCFLAGS) $< $(BUILD)/lib/libmihomo_tui.a $(LDLIBS) -o $@

APP_TEST_OBJS := $(filter-out $(BUILD)/obj/app/main.o,$(APP_OBJS))

$(BUILD)/test/test_logfmt: test/test_logfmt.cpp $(BUILD)/obj/app/logfmt.o $(BUILD)/obj/app/common.o $(BUILD)/lib/libmihomo_tui.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(HARDWARN) $(CXXFLAGS) $(INCFLAGS) $< $(BUILD)/obj/app/logfmt.o $(BUILD)/obj/app/common.o $(BUILD)/lib/libmihomo_tui.a $(LDLIBS) -o $@

$(BUILD)/test/test_use: test/test_use.cpp $(BUILD)/lib/libmihomo_tui.a $(APP_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(HARDWARN) $(CXXFLAGS) $(INCFLAGS) $< $(APP_TEST_OBJS) $(BUILD)/lib/libmihomo_tui.a $(LDLIBS) -o $@

test: $(TEST_TARGETS) $(BIN)
	@echo "== 单元测试 =="
	@$(BUILD)/test/test_widgets
	@$(BUILD)/test/test_use
	@$(BUILD)/test/test_logfmt
	@echo "== snapshot 回归 =="
	@test/run_snapshots.sh

check:
	@echo "== 头文件检查 =="
	@for h in $$(find include -name '*.hpp' | sort); do \
	  $(CXX) $(CXXSTD) $(HARDWARN) $(INCFLAGS) -fsyntax-only -x c++-header $$h || { echo "  FAIL $$h"; exit 1; }; \
	done
	@echo "== 头文件检查通过 =="

run: $(BIN)
	@$(BIN)

demo: $(BIN)
	@$(BIN) --demo-layout

clean:
	@rm -rf $(BUILD)
	@echo "  已清理 $(BUILD)/"
