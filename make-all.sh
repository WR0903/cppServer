args=$1

buildType="Debug"
clean=false

if [ "${args}v" = "release"v ];then
    buildType="Release"
elif [ "${args}v" = "clean"v ];then
    clean=true
fi

BUILD_DIR="build"

if ${clean};then
    rm -rf ${BUILD_DIR}
else
    mkdir -p ${BUILD_DIR}
    cd ${BUILD_DIR}
    cmake -DCMAKE_BUILD_TYPE=${buildType} ..
    # 限制并行作业数，避免 allinone 等合并编译目标内存峰值触发 OOM
    make -j8
fi
