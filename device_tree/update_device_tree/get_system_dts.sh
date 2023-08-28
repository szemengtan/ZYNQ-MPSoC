if ! [ -x "$(command -v dtc)" ]; then
    sudo apt-get install -y device-tree-compiler
fi
if ! [ -x "$(command -v dumpimage)" ]; then
    sudo apt-get install -y u-boot-tools
fi
dtc -I fs /proc/device-tree > device-tree0.dts

if mountpoint -q /mnt; then
    echo "/mnt already mounted"
else
    sudo mount /dev/mmcblk1p1 /mnt
fi

read -p "Back up existing BOOT.bin and image.ub? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
    echo "Backing up BOOT.bin and image.ub"
    sudo cp /mnt/image.ub .
    sudo chown $USER:$USER image.ub
    sudo cp /mnt/BOOT.bin .
    sudo chown $USER:$USER BOOT.bin
else
    echo "Skipping backup"
fi
sudo cp /mnt/image.ub .
sudo chown $USER:$USER image.ub
sudo cp /mnt/BOOT.bin .
sudo chown $USER:$USER BOOT.bin
pushd /mnt
sudo dumpimage -T flat_dt -p 1 -o device-tree1.dtb image.ub
sudo dtc -I dtb -O dts -o device-tree1.dts device-tree1.dtb
popd
sudo cp /mnt/device-tree1.dts system.dts
sudo chown $USER:$USER system.dts
echo "Edit the file system.dts to include required functionality"