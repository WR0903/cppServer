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
    make -j1
fi
