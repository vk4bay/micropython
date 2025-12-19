cd /home/bob/projects/bdars/esp-idf/
source export.sh

cd /home/bob/projects/bdars/micropython/ports/esp32
rm -rf build-BDARS_S3
make BOARD=BDARS_S3 submodules
idf.py -D MICROPY_BOARD=BDARS_S3 reconfigure
make BOARD=BDARS_S3
cp ../../build-BDARS_S3/firmware.bin /mnt/c/tmp

