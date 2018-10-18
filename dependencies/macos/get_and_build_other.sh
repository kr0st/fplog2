#!/bin/bash

cmake --version 2> /dev/null
if [ $? -ne 0 ]; then
echo "************** ERROR **************"
echo "Please install CMake"
echo "***********************************"
exit
fi

curl --version > /dev/null
if [ $? -ne 0 ]; then
echo "************** ERROR **************"
echo "Please install curl"
echo "***********************************"
exit
fi

7za > /dev/null
if [ $? -ne 0 ]; then
echo "********************* ERROR *********************"
echo "Please install 7zip (p7zip-full package)"
echo "*************************************************"
exit
fi

curl --cacert ./cacert.crt --output cacert.pem https://curl.haxx.se/ca/cacert.pem
if [ $? -eq 0 ]; then
rm cacert.crt
mv cacert.pem cacert.crt
fi

rm -rf ../build
mkdir ../build

curl -LR --cacert ./cacert.crt -o ../build/rapidjson.zip https://github.com/Tencent/rapidjson/archive/v1.1.0.zip
curl -L --cacert ./cacert.crt -o ../build/date.zip https://github.com/HowardHinnant/date/archive/v2.2.zip
curl -L --cacert ./cacert.crt -o ../build/gtest.zip https://codeload.github.com/google/googletest/zip/release-1.8.0

7za x -y -o"../build/" ../build/rapidjson.zip
7za x -y -o"../build/" ../build/date.zip
7za x -y -o"../build/" ../build/gtest.zip

export LDFLAGS="-L/usr/local/opt/openssl/lib"
export CPPFLAGS="-I/usr/local/opt/openssl/include"

cd ..
cd ./build

mkdir ../include

mkdir ../lib
mkdir ../lib/x64

rm -rf ../include/rapidjson

cd rapidjson-1.1.0
cp -rf ./include/* ../../include/
cd ..

cd googletest-release-1.8.0
cd googletest

rm -rf ../../../include/gtest
rm -rf ../../../lib/x64/gtest

cmake ./
make

if [ $? -ne 0 ]; then
echo "****************************************** ERROR ******************************************"
echo "unable to build gtest libraries, please inspect console output for clues."
echo "*******************************************************************************************"
exit
fi

cp -rf ./libgtest.a ../../../lib/x64
cp -rf ./libgtest_main.a ../../../lib/x64
cp -rf ./include/gtest ../../../include/

cd ..
cd ..

rm -rf ../include/date
mkdir ../include/date

cp -rf ./date-2.2/* ../include/date/

cd ..
rm -rf ./build

