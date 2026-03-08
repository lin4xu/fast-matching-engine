#!/bin/bash

# 遇到任何错误立即退出，防止带着 Bug 跑分
set -e

echo "🚀 [1/3] 检查构建环境..."
# 只有在 build 文件夹不存在时，才执行耗时的配置动作
if [ ! -d "build" ]; then
    echo "📁 未检测到 build 目录，正在首次生成 CMake 配置..."
    cmake -B build
else
    echo "✅ build 目录已存在，跳过 CMake 配置阶段。"
fi

echo "🔨 [2/3] 极致增量编译 Release 版本 (开启多核加速)..."
# -j 选项会让编译器调用你 CPU 的多个核心同时干活，速度起飞！
cmake --build build --config Release -j 8

echo "🔥 [3/3] 编译成功！开始执行终极大满贯跑分！"
echo "================================================================="
./build/Release/run_benchmarks.exe
echo "================================================================="
echo "🎉 跑分结束！"