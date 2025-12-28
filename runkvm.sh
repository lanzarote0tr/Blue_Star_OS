./build.sh

qemu-system-x86_64 \
    -bios OVMF.fd \
    -drive if=pflash,format=raw,readonly=on,file=OVMF.fd \
    -drive file=fat:rw:EFIfile,format=raw \
    -net none \
    -m 512M \
    -monitor stdio \
    -enable-kvm -cpu host
