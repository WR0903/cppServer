args=$1

buildType="Debug"
clean=false

if [ ${args}v = "release"v ];then
    buildType="Release"
elif [ ${args}v = "clean"v ];then
    clean=true
fi


build(){
    for i in `ls -d */`;do 
        cd $i
        if ${clean};then
            rm -rf build            
        else
            mkdir -p build
            cd build
            cmake -DCMAKE_BUILD_TYPE=${buildType} ../
            make -j4
            cd ..
        fi
        cd ..
    done
}

# cd src/libs/
# build

cd src/apps/
build

cd ../tools/
build
