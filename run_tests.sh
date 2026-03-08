#!/bin/bash

set -e

echo "🐛 [1/3] 检查 Debug 测试构建环境..."
if [ ! -d "build_debug" ]; then
    echo "📁 未检测到 build_debug 目录，正在首次生成 Debug 模式的 CMake 配置..."
    cmake -B build_debug -DCMAKE_BUILD_TYPE=Debug
else
    echo "✅ build_debug 目录已存在，跳过 CMake 配置阶段。"
fi

echo "🔨 [2/3] 编译正确性测试程序 (Debug 模式, 开启多核加速)..."
cmake --build build_debug -j 8

echo "🧪 [3/3] 编译成功！开始执行终极一致性校验 (Shadow Testing)..."
echo "================================================================="
# 【修复】：补充 Windows MSVC 特有的 Debug 子目录
./build_debug/Debug/matching_tests.exe
echo "================================================================="
echo "🎉 校验结束！如果上方全是绿色的 [  PASSED  ]，说明你的优化毫无破绽！"