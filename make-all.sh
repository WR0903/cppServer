args=$1

buildType="Debug"
clean=false

if [ "${args}v" = "release"v ];then
    buildType="Release"
elif [ "${args}v" = "clean"v ];then
    clean=true
fi

BUILD_DIR="build"

# 代码中使用 #include <hiredis/hiredis.h>，但 3rdparty 下目录名为 hiredis-master，
# 建立软链接 hiredis -> hiredis-master 让头文件路径正确解析（不影响 git 跟踪）。
if [ ! -e "src/3rdparty/hiredis" ]; then
    ln -sfn hiredis-master src/3rdparty/hiredis
fi

if ${clean};then
    rm -rf ${BUILD_DIR}
else
    mkdir -p ${BUILD_DIR}
    cd ${BUILD_DIR}
    cmake -DCMAKE_BUILD_TYPE=${buildType} ..
    # 限制并行作业数，避免 allinone 等合并编译目标内存峰值触发 OOM
    make -j8
fi
