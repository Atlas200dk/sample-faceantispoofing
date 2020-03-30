#!/bin/sh

i=$(ps -ef | grep presenter | grep face_detection | grep -o '[0-9]\+' | head -n1)
if [ -z "$i" ] ;then
echo presenter sever not in process!
exit
fi
kill -9 $i
echo presenter sever stop success!


