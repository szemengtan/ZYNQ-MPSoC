if ! dtc -I dts -O dtb -o system.dtb system.dts; then
    echo "dtc returned an error"
else
    if mountpoint -q /mnt; then
        echo "/mnt already mounted"
    else
        sudo mount /dev/mmcblk1p1 /mnt
    fi
    sudo cp system.dtb /mnt
    pushd /mnt
    sudo mkimage -f image.its image.ub
    popd
    echo "image.ub has been updated, reboot for changes to take effect"
fi
